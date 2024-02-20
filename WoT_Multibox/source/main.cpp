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
	ULONG size = 0x20;
	PPROCESS_HANDLE_SNAPSHOT_INFORMATION handle_snapshot_information = nullptr;
	do
	{
		// Allocate buffer for our handle snapshot information
		PVOID buffer = realloc(handle_snapshot_information, size *= 2);

		// Make sure it is non-null
		if (!buffer)
			continue;

		// Assign to the higher-level pointer
		handle_snapshot_information = reinterpret_cast<PPROCESS_HANDLE_SNAPSHOT_INFORMATION>(buffer);

		// Try to query info to update status code
		last_status = NtQueryInformationProcess(
			ProcessHandle,
			ProcessHandleInformation,
			handle_snapshot_information,
			size,
			nullptr
		);

	} while (last_status == STATUS_INFO_LENGTH_MISMATCH);

	if (!handle_snapshot_information)
	{
		last_status = STATUS_NO_MEMORY;
		goto cleanup;
	}

	// Skip this process if we failed to allocate stuff
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

		if (!NT_SUCCESS(last_status))
			continue;

		// Figure out what size is needed for the name
		ULONG return_length = sizeof(UNICODE_STRING);
		PUNICODE_STRING object_name = nullptr;
		do
		{
			object_name = reinterpret_cast<PUNICODE_STRING>(realloc(object_name, return_length *= 2));

			last_status = NtQueryObject(
				duplicated_handle,
				ObjectNameInformation,
				object_name,
				return_length,
				&return_length
			);

		} while (last_status == STATUS_INFO_LENGTH_MISMATCH);

		// Close the duplicated handle, no need to keep it open anymore
		CloseHandle(duplicated_handle);

		// Skip any unnamed handles
		if (!NT_SUCCESS(last_status) || !object_name || !object_name->Buffer)
		{
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

		if (object_name)
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