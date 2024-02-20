#include "winapi.hpp"
#include <vector>
#include <TlHelp32.h>

#ifndef _WIN64
#error "Compile in x64!"
#endif // _WIN64

std::vector<ULONG> LocateProcessIdsByImageName(
	IN LPCWSTR ImageName
)
{
	HANDLE snapshot_handle = CreateToolhelp32Snapshot(
		TH32CS_SNAPPROCESS,
		1337
	);

	if (snapshot_handle == INVALID_HANDLE_VALUE)
		return {};

	PROCESSENTRY32 entry = {
		.dwSize = sizeof(PROCESSENTRY32)
	};

	std::vector<ULONG> process_ids;
	Process32First(snapshot_handle, &entry);
	do
	{
		if (!_wcsicmp(entry.szExeFile, ImageName))
			process_ids.push_back(entry.th32ProcessID);
		
	} while (Process32Next(snapshot_handle, &entry));

	CloseHandle(snapshot_handle);
	return process_ids;
}

NTSTATUS AuditProcessHandles(
	HANDLE ProcessHandle
)
{
	NTSTATUS last_status = STATUS_SUCCESS;

	// Capture all handles for the process
	PPROCESS_HANDLE_SNAPSHOT_INFORMATION handle_snapshot_information = nullptr;
	ULONG size_required = 0x10;
	
	// Determine the size needed for the handle information
	// by calling the function with an insufficiently-sized buffer
	do
	{
		size_required *= 2;
		
		// IntelliSense is wrong here, this doesn't leak memory
		handle_snapshot_information = reinterpret_cast<PPROCESS_HANDLE_SNAPSHOT_INFORMATION>(
			realloc(handle_snapshot_information, size_required)
		);
		
		// Failed to allocate? Try again...
		if (!handle_snapshot_information)
			continue;

		last_status = NtQueryInformationProcess(
			ProcessHandle,
			ProcessHandleInformation,
			handle_snapshot_information,
			size_required,
			nullptr
		);

	} while (last_status == STATUS_INFO_LENGTH_MISMATCH);

	// The function should not return any error code other than
	// complaining about our buffer being too small
	if (!NT_SUCCESS(last_status))
	{
		goto cleanup;
	}

	// Try to allocate memory for the snapshot buffer
	handle_snapshot_information = reinterpret_cast<PPROCESS_HANDLE_SNAPSHOT_INFORMATION>(
		malloc(size_required)
	);

	if (!handle_snapshot_information)
	{
		last_status = STATUS_NO_MEMORY;
		goto cleanup;
	}

	// Call the function again, this time with our buffer
	last_status = NtQueryInformationProcess(
		ProcessHandle,
		ProcessHandleInformation,
		handle_snapshot_information,
		size_required,
		nullptr
	);

	// Skip this iteration if the second call failed
	if (!NT_SUCCESS(last_status))
	{
		goto cleanup;
	}	

	// Loop all handles the process has open
	for (size_t i = 0; i < handle_snapshot_information->NumberOfHandles; i++)
	{
		HANDLE duplicated_handle = nullptr;

		// Duplicate the handle from the process for closer examination
		last_status = NtDuplicateObject(
			ProcessHandle,
			handle_snapshot_information->Handles[i].HandleValue,
			GetCurrentProcess(),
			&duplicated_handle,
			0,
			0,
			DUPLICATE_SAME_ACCESS
		);

		// Make sure we duplicated the handle properly
		if (!NT_SUCCESS(last_status))
			continue;

		ULONG object_name_size = 0;
		PUNICODE_STRING object_name = nullptr;

		// Figure out what size is needed for the name
		last_status = NtQueryObject(
			duplicated_handle,
			ObjectNameInformation,
			object_name,
			0,
			&object_name_size
		);

		// The function should not return any error code other than
		// complaining about our buffer being too small
		if (last_status != STATUS_INFO_LENGTH_MISMATCH)
		{
			CloseHandle(duplicated_handle);
			continue;
		}

		// Try to allocate memory for the string buffer
		object_name = reinterpret_cast<PUNICODE_STRING>(
			malloc(size_required)
		);

		// Make sure the memory got allocated
		if (!object_name)
		{
			CloseHandle(duplicated_handle);
			continue;
		}

		// Try to query again, this time with the correct size
		last_status = NtQueryObject(
			duplicated_handle,
			ObjectNameInformation,
			object_name,
			object_name_size,
			nullptr
		);

		// Close the duplicated handle, no need to keep it open anymore
		CloseHandle(duplicated_handle);

		// Skip any unnamed handles
		if (!NT_SUCCESS(last_status) || !object_name->Buffer)
		{
			if (object_name)
				free(object_name);

			continue;
		}

		// Determine if the handle needs to be closed by checking the name for these strings
		if (wcsstr(object_name->Buffer, L"wgc_game_mtx_") || wcsstr(object_name->Buffer, L"wgc_running_games_mtx"))
		{
			HANDLE mtx_handle = nullptr;
			// Close the source handle
			last_status = NtDuplicateObject(
				ProcessHandle,
				handle_snapshot_information->Handles[i].HandleValue,
				GetCurrentProcess(),
				&mtx_handle,
				0,
				0,
				DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS
			);

			if (!NT_SUCCESS(last_status))
			{
				printf(
					"Failed to close handle '%wZ' of process %d (status code 0x%08X)\n", 
					object_name, 
					GetProcessId(ProcessHandle), 
					last_status
				);

				continue;
			}

			printf(
				"Closed mutant '%wZ' of process %d\n",
				object_name,
				GetProcessId(ProcessHandle)
			);

			CloseHandle(mtx_handle);
		}

		free(object_name);
	}

	last_status = STATUS_SUCCESS;
cleanup:
	if (handle_snapshot_information)
		free(handle_snapshot_information);

	return last_status;
}

int main()
{
	// Disable the console quick edit mode - this prevents copypasting, but also 
	// prevents accidental clicks into the console from suspending the entire tool.
	SetConsoleTitleA("Glorified NtQueryObject caller");

	HANDLE input_stream = GetStdHandle(STD_INPUT_HANDLE);
	DWORD console_mode;
	GetConsoleMode(input_stream, &console_mode);
	SetConsoleMode(input_stream, ENABLE_EXTENDED_FLAGS | (console_mode & ~ENABLE_QUICK_EDIT_MODE));

	// First we find the World of Tanks processes, as
	// they themselves create the mutant objects.
	while (true)
	{
		auto process_ids = LocateProcessIdsByImageName(
			L"WorldOfTanks.exe"
		);

		for (auto process_id : process_ids)
		{
			// Query Information is required for NtQueryObject and ProcessHandleInformation
			// Duplicate Handles is required for DuplicateHandle to close the mutant remotely
			HANDLE process_handle = OpenProcess(
				PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE,
				false,
				process_id
			);

			// Make sure we opened the process
			if (!process_handle)
				continue;

			NTSTATUS last_status = AuditProcessHandles(process_handle);
			if (!NT_SUCCESS(last_status))
			{
				printf(
					"Failed to audit handles of process %d (status code 0x%08X)\n",
					GetProcessId(process_handle),
					last_status
				);
			}

			CloseHandle(process_handle);
		}

		Sleep(100);
	}
}