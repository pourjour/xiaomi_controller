#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <ViGEm/Client.h>

// Headers for HID device enumeration
#include <hidsdi.h>
#include <Setupapi.h>

// Link against SetupAPI and HID libraries
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

// --- Function Declarations ---
std::optional<HANDLE> find_xiaomi_gamepad(USHORT vendor_id, USHORT product_id);
void parse_and_map_report(const char* report, DWORD report_size, PHIDP_PREPARSED_DATA preparsed_data, XUSB_REPORT& xbox_report);
long scale_axis(long value, long hid_min, long hid_max, long xbox_min, long xbox_max);

int main() {
	std::cout << "Xiaomi Controller to Xbox 360 Emulator" << std::endl;

	// --- 1. Initialize ViGEm client ---
	const auto client = vigem_alloc();
	if (client == nullptr) {
		std::cerr << "Error: Failed to allocate ViGEm client." << std::endl;
		return -1;
	}

	const auto retval = vigem_connect(client);
	if (!VIGEM_SUCCESS(retval)) {
		std::cerr << "Error: ViGEm Bus connection failed with error code: 0x" << std::hex << retval << std::endl;
		vigem_free(client);
		return -1;
	}

	// --- 2. Allocate a virtual Xbox 360 controller ---
	const auto pad = vigem_target_x360_alloc();
	const auto pir = vigem_target_add(client, pad);
	if (!VIGEM_SUCCESS(pir)) {
		std::cerr << "Error: Target plugin failed with error code: 0x" << std::hex << pir << std::endl;
		vigem_disconnect(client);
		vigem_free(client);
		return -1;
	}
	std::cout << "Virtual Xbox 360 controller created." << std::endl;

	// --- 3. Find the physical Xiaomi gamepad ---
	USHORT vendor_id = 0x2717;
	USHORT product_id = 0x5067;
	
	std::cout << "Searching for Xiaomi gamepad (VID: 0x" << std::hex << vendor_id << ", PID: 0x" << product_id << ")..." << std::endl;
	
	std::optional<HANDLE> gamepad_handle_opt = find_xiaomi_gamepad(vendor_id, product_id);

	if (!gamepad_handle_opt) {
		std::cerr << "Xiaomi gamepad not found. Please ensure it is connected." << std::endl;
		vigem_target_remove(client, pad);
		vigem_target_free(pad);
		vigem_disconnect(client);
		vigem_free(client);
		system("pause");
		return -1;
	}
	HANDLE gamepad_handle = *gamepad_handle_opt;
	std::cout << "Xiaomi gamepad found!" << std::endl;

	// --- 4. Get HID Info ---
	PHIDP_PREPARSED_DATA preparsed_data;
	if (!HidD_GetPreparsedData(gamepad_handle, &preparsed_data)) {
		std::cerr << "Error: HidD_GetPreparsedData failed." << std::endl;
		CloseHandle(gamepad_handle);
		// ... vigem cleanup
		return -1;
	}

	HIDP_CAPS caps;
	HidP_GetCaps(preparsed_data, &caps);
	std::vector<char> report_buffer(caps.InputReportByteLength);

	// --- 5. Main emulation loop ---
	XUSB_REPORT xbox_report;
	XUSB_REPORT_INIT(&xbox_report);

	std::cout << "Starting emulation loop. Press Ctrl+C to exit." << std::endl;
	while (true) {
		DWORD bytes_read;
		if (ReadFile(gamepad_handle, report_buffer.data(), static_cast<DWORD>(report_buffer.size()), &bytes_read, NULL)) {
			if (bytes_read > 0) {
				parse_and_map_report(report_buffer.data(), bytes_read, preparsed_data, xbox_report);
				vigem_target_x360_update(client, pad, xbox_report);
			}
		} else {
			 std::cerr << "Error reading from gamepad. Error code: " << GetLastError() << std::endl;
			 std::this_thread::sleep_for(std::chrono::seconds(1)); // Avoid spamming errors
		}
	}

	// --- 6. Cleanup ---
	CloseHandle(gamepad_handle);
	HidD_FreePreparsedData(preparsed_data);
	vigem_target_remove(client, pad);
	vigem_target_free(pad);
	vigem_disconnect(client);
	vigem_free(client);

	return 0;
}

std::optional<HANDLE> find_xiaomi_gamepad(USHORT vendor_id, USHORT product_id) {
	GUID hid_guid;
	HidD_GetHidGuid(&hid_guid);
	
	HDEVINFO dev_info = SetupDiGetClassDevs(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		return std::nullopt;
	}

	SP_DEVICE_INTERFACE_DATA dev_interface_data;
	dev_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, i, &dev_interface_data); ++i) {
		DWORD required_size = 0;
		SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data, NULL, 0, &required_size, NULL);
		
		std::vector<BYTE> detail_buffer(required_size);
		auto dev_interface_detail_data = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detail_buffer.data());
		dev_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		
		if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data, dev_interface_detail_data, required_size, NULL, NULL)) {
			continue;
		}

		HANDLE device_handle = CreateFile(dev_interface_detail_data->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (device_handle == INVALID_HANDLE_VALUE) {
			continue;
		}

		HIDD_ATTRIBUTES attributes;
		attributes.Size = sizeof(HIDD_ATTRIBUTES);
		if (HidD_GetAttributes(device_handle, &attributes)) {
			if (attributes.VendorID == vendor_id && attributes.ProductID == product_id) {
				SetupDiDestroyDeviceInfoList(dev_info);
				return device_handle;
			}
		}
		CloseHandle(device_handle);
	}

	SetupDiDestroyDeviceInfoList(dev_info);
	return std::nullopt;
}

void parse_and_map_report(const char* report, DWORD report_size, PHIDP_PREPARSED_DATA preparsed_data, XUSB_REPORT& xbox_report) {
	HIDP_CAPS caps;
	HidP_GetCaps(preparsed_data, &caps);

	// --- Reset Buttons and Triggers ---
	xbox_report.wButtons = 0;
	xbox_report.bLeftTrigger = 0;
	xbox_report.bRightTrigger = 0;

	// --- Handle Buttons ---
	USHORT buttonCapsLen = caps.NumberInputButtonCaps;
	if (buttonCapsLen > 0) {
		std::vector<HIDP_BUTTON_CAPS> buttonCaps(buttonCapsLen);
		if (HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCapsLen, preparsed_data) == HIDP_STATUS_SUCCESS && buttonCapsLen > 0) {
			USAGE usagePage = buttonCaps[0].UsagePage;
			ULONG usageLength = 64; // reasonable buffer size for pressed buttons
			std::vector<USAGE> usageList(usageLength);
			if (HidP_GetUsages(HidP_Input, usagePage, 0, usageList.data(), &usageLength, preparsed_data, (PCHAR)report, report_size) == HIDP_STATUS_SUCCESS) {
				for (ULONG i = 0; i < usageLength; ++i) {
					USAGE u = usageList[i];
					// NOTE: Adjust mappings as needed for your device
					switch (u) {
						case 1: xbox_report.wButtons |= XUSB_GAMEPAD_A; break;
						case 2: xbox_report.wButtons |= XUSB_GAMEPAD_B; break;
						case 4: xbox_report.wButtons |= XUSB_GAMEPAD_X; break;
						case 5: xbox_report.wButtons |= XUSB_GAMEPAD_Y; break;
						case 7: xbox_report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER; break;
						case 8: xbox_report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER; break;
						case 11: xbox_report.wButtons |= XUSB_GAMEPAD_BACK; break;
						case 12: xbox_report.wButtons |= XUSB_GAMEPAD_START; break;
						case 14: xbox_report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB; break;
						case 15: xbox_report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB; break;
						default: break;
					}
				}
			}
		}
	}
	
	// --- Handle Axes and Triggers ---
	USHORT valueCapsLen = caps.NumberInputValueCaps;
	if (valueCapsLen > 0) {
		std::vector<HIDP_VALUE_CAPS> value_caps(valueCapsLen);
		if (HidP_GetValueCaps(HidP_Input, value_caps.data(), &valueCapsLen, preparsed_data) == HIDP_STATUS_SUCCESS) {
			for (ULONG i = 0; i < valueCapsLen; ++i) {
				const auto& v_cap = value_caps[i];
				USAGE usage = v_cap.IsRange ? v_cap.Range.UsageMin : v_cap.NotRange.Usage;
				ULONG value = 0;
				if (HidP_GetUsageValue(HidP_Input, v_cap.UsagePage, 0, usage, &value, preparsed_data, (PCHAR)report, report_size) != HIDP_STATUS_SUCCESS) {
					continue;
				}

				long lmin = v_cap.LogicalMin;
				long lmax = v_cap.LogicalMax;

				switch (usage) {
					case 0x30: // Left Stick X
						xbox_report.sThumbLX = static_cast<SHORT>(scale_axis(static_cast<long>(value), lmin, lmax, -32768, 32767));
						break;
					case 0x31: // Left Stick Y
						xbox_report.sThumbLY = static_cast<SHORT>(-scale_axis(static_cast<long>(value), lmin, lmax, -32768, 32767)); // Invert Y-axis
						break;
					case 0x32: // Right Stick X (Sometimes Z-Axis)
						xbox_report.sThumbRX = static_cast<SHORT>(scale_axis(static_cast<long>(value), lmin, lmax, -32768, 32767));
						break;
					case 0x35: // Right Stick Y (Sometimes Z-Rotation)
						xbox_report.sThumbRY = static_cast<SHORT>(-scale_axis(static_cast<long>(value), lmin, lmax, -32768, 32767)); // Invert Y-axis
						break;
					case 0x33: // Left Trigger? (Sometimes Rx)
					case 0x36: // Slider (Alternative trigger mapping)
					case 0xc5: // Xiaomi Left Trigger
						xbox_report.bLeftTrigger = static_cast<BYTE>(scale_axis(static_cast<long>(value), lmin, lmax, 0, 255));
						break;
					case 0x34: // Right Trigger? (Sometimes Ry)  
					case 0x37: // Dial (Alternative trigger mapping)
					case 0xc4: // Xiaomi Right Trigger
						xbox_report.bRightTrigger = static_cast<BYTE>(scale_axis(static_cast<long>(value), lmin, lmax, 0, 255));
						break;
					case 0x39: // Hat Switch (DPAD)
						// Hat values 0-7 (8 is neutral on some devices)
						if (value >= static_cast<ULONG>(lmin) && value <= static_cast<ULONG>(lmax)) {
							switch (value) {
								case 0: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_UP; break;
								case 1: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_RIGHT; break;
								case 2: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT; break;
								case 3: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_RIGHT; break;
								case 4: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN; break;
								case 5: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN | XUSB_GAMEPAD_DPAD_LEFT; break;
								case 6: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT; break;
								case 7: xbox_report.wButtons |= XUSB_GAMEPAD_DPAD_UP | XUSB_GAMEPAD_DPAD_LEFT; break;
								default: break;
							}
						}
						break;
					default:
						break;
				}
			}
		}
	}
}

long scale_axis(long value, long hid_min, long hid_max, long xbox_min, long xbox_max) {
	// Basic scaling and deadzone
	long hid_range = hid_max - hid_min;
	if (hid_range == 0) return (xbox_max + xbox_min) / 2;
	long xbox_range = xbox_max - xbox_min;

	// Apply a 10% deadzone around center if symmetric
	long hid_center = hid_min + hid_range / 2;
	long deadzone = hid_range / 10;
	if (std::llabs(static_cast<long long>(value) - hid_center) < deadzone) {
		return (xbox_max + xbox_min) / 2;
	}

	double normalized_value = static_cast<double>(value - hid_min) / static_cast<double>(hid_range);
	return static_cast<long>(normalized_value * xbox_range + xbox_min);
} 