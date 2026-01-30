// Capture raw audio from "Line In" (or default input) using PortAudio
// Reads audio in fixed-size buffers (frames per buffer) and calls process_buffer()
// Build:
//   Debian/Ubuntu: sudo apt-get install libportaudio2 portaudio19-dev
//   Windows: download PortAudio binaries and set up include/lib paths
//   Compile:
//     g++ -std=c++17 -O2 -o read_line_in_audio read_line_in_audio.cpp -lportaudio
//
// Usage:
//   ./read_line_in_audio                # default: 4096 frames, stereo, 44100 Hz, auto device selection
//   ./read_line_in_audio 2048 1 48000  # framesPerBuffer=2048, channels=1, sampleRate=48000
//   ./read_line_in_audio --list-devices
//   ./read_line_in_audio --device 3
//   ./read_line_in_audio 4096 2 44100 --device 3
//
// The program will capture signed 16-bit little-endian PCM (paInt16).
// To save raw PCM to a file, redirect stdout: ./read_line_in_audio > capture.raw
//
// Note: When redirecting stdout, progress logging still uses stderr.
//
// process_buffer() is the place to add your own handling.

#include "portaudio.h"

#include <atomic>
#include <csignal>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

static std::atomic<bool> g_stop{false};

void handle_sigint(int)
{
	(void)int(); // silence unused param in some toolchains - but keep signature
    g_stop = true;
}

static void to_lower(std::string& s)
{
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Hook: process captured buffer (int16 samples), frames = number of frames, channels = channels per frame.
// By default, we write the raw bytes to stdout (so user can redirect). Replace or extend behavior here.
// E.g. write to WAV, get frequency spectrum, etc.
void process_buffer(const int16_t* samples, size_t frames, int channels)
{
	// Example default behavior: write raw PCM (little-endian s16) to stdout
    // This makes it easy to pipe into a file or another program.
    size_t samples_count = frames * static_cast<size_t>(channels);
    if (samples_count == 0) return;

    // Write binary data to stdout
    size_t bytes_to_write = samples_count * sizeof(int16_t);
    const char* p = reinterpret_cast<const char*>(samples);

    // Use fwrite to avoid C++ iostream buffering issues with binary output
    size_t written = fwrite(p, 1, bytes_to_write, stdout);
    (void)written; // ignore for now; could add error handling
    fflush(stdout);
}

void list_devices_and_exit()
{
	int numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		std::cerr << "ERROR: Pa_GetDeviceCount returned " << numDevices << "\n";
		return;
	}
	std::cerr << "Available PortAudio devices:\n";
	for (int i = 0; i < numDevices; ++i) {
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		if (!info) continue;
		std::cout << "Index " << i << ": " << info->name
			<< " (hostApi = " << Pa_GetHostApiInfo(info->hostApi)->name << ")"
			<< " maxInputChannels=" << info->maxInputChannels
			<< " defaultSampleRate=" << info->defaultSampleRate
			<< "\n";
	}
}

std::optional<int> find_line_in_device(int numDevices)
{
	for (int i = 0; i < numDevices; ++i) {
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		if (!info) continue;
		std::string name = info->name ? info->name : "";
		std::string low = name;
		to_lower(low);
		if (low.find("line") != std::string::npos || low.find("line in") != std::string::npos ||
			low.find("line-in") != std::string::npos || low.find("stereo mix") != std::string::npos) {
			if (info->maxInputChannels > 0) return i;
		}
	}
	return std::nullopt;
}

int main(int argc, char* argv[])
{
	std::signal(SIGINT, [](int)
	{
		g_stop = true;
	});

	// Defaults
	unsigned long framesPerBuffer = 4096;
	int channels = 2;
	double sampleRate = 44100.0;
	std::optional<int> explicitDeviceIndex;

	// Simple argument parsing
	for (int i = 1; i < argc; ++i)
	{
		std::string a = argv[i];
		if (a == "--list-devices" || a == "-l")
		{
			std::cout << "Listing available audio input devices:\n";
			if (Pa_Initialize() != paNoError) {
				std::cout << "Failed to initialize PortAudio for listing devices\n";
				return 1;
			}
			list_devices_and_exit();
			Pa_Terminate();
			return 0;
		}
		else if (a == "--device" && i + 1 < argc)
		{
			explicitDeviceIndex = std::stoi(argv[++i]);
		}
		else if (a.size() >= 2 && a[0] == '-' && a[1] == '-')
		{
			// unknown long option, ignore
		}
		else if (std::isdigit(static_cast<unsigned char>(a[0])))
		{
			// positional numeric args: framesPerBuffer [channels] [sampleRate]
			if (framesPerBuffer == 0) {
				framesPerBuffer = static_cast<unsigned long>(std::stoul(a));
			}
			else if (channels == 0) {
				channels = std::stoi(a);
			}
		}
		else
		{
			// try to parse as first, second or third positional if given in order
			// We'll allow up to 3 positional arguments: framesPerBuffer, channels, sampleRate
			// Re-parse from argv directly below for simplicity.
		}
	}

	// Re-parse positional numeric args in order from argv[1..]
	// This is to allow the user to call: ./read_line_in_audio 2048 1 48000 --device 3
	int pos = 1;
	if (pos < argc && std::isdigit(static_cast<unsigned char>(argv[pos][0])))
	{
		framesPerBuffer = static_cast<unsigned long>(std::stoul(argv[pos]));
		++pos;
		if (pos < argc && std::isdigit(static_cast<unsigned char>(argv[pos][0])))
		{
			channels = std::stoi(argv[pos]);
			++pos;
			if (pos < argc && (std::isdigit(static_cast<unsigned char>(argv[pos][0])) || argv[pos][0] == '.'))
			{
				sampleRate = std::stod(argv[pos]);
				++pos;
			}
		}
	}

	PaError err = Pa_Initialize();
	if (err != paNoError) {
		std::cerr << "PortAudio initialize error: " << Pa_GetErrorText(err) << "\n";
		return 1;
	}

	int numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		std::cerr << "Pa_GetDeviceCount error: " << numDevices << "\n";
		Pa_Terminate();
		return 1;
	}

	int inputDevice = paNoDevice;

	if (explicitDeviceIndex.has_value()) {
        inputDevice = explicitDeviceIndex.value();
        if (inputDevice < 0 || inputDevice >= numDevices) {
            std::cerr << "Invalid device index: " << inputDevice << "\n";
            Pa_Terminate();
            return 1;
        }
        const PaDeviceInfo* info = Pa_GetDeviceInfo(inputDevice);
        if (!info || info->maxInputChannels <= 0) {
            std::cerr << "Selected device has no input channels.\n";
            Pa_Terminate();
            return 1;
        }
	}
	else
    {
        // Try to find a "line in" device name first
        auto found = find_line_in_device(numDevices);
        if (found.has_value()) {
            inputDevice = found.value();
			std::cerr << "Using device index " << inputDevice << " (matched 'line') : "
				<< Pa_GetDeviceInfo(inputDevice)->name << "\n";
		}
		else {
			// fallback to default input device
			inputDevice = Pa_GetDefaultInputDevice();
			if (inputDevice == paNoDevice) {
                std::cerr << "No default input device.\n";
                Pa_Terminate();
                return 1;
            }
            std::cerr << "Using default input device index " << inputDevice << " : "
                      << Pa_GetDeviceInfo(inputDevice)->name << "\n";
        }
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputDevice);
    if (!deviceInfo) {
        std::cerr << "Failed to get device info for index " << inputDevice << "\n";
        Pa_Terminate();
        return 1;
    }

    if (deviceInfo->maxInputChannels < channels) {
        std::cerr << "Device supports only " << deviceInfo->maxInputChannels << " input channels, but requested "
                  << channels << ". Reducing channels to " << deviceInfo->maxInputChannels << ".\n";
        channels = static_cast<int>(deviceInfo->maxInputChannels);
        if (channels <= 0) {
            std::cerr << "No input channels available.\n";
            Pa_Terminate();
            return 1;
        }
    }

    PaStreamParameters inputParams;
    memset(&inputParams, 0, sizeof(inputParams));
    inputParams.device = inputDevice;
    inputParams.channelCount = channels;
    inputParams.sampleFormat = paInt16;
    inputParams.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream = nullptr;

    err = Pa_OpenStream(&stream,
                        &inputParams,
                        nullptr, // no output
                        sampleRate,
                        framesPerBuffer,
                        paClipOff,
                        nullptr, // no callback, we'll use blocking reads
                        nullptr);
    if (err != paNoError) {
        std::cerr << "Pa_OpenStream error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Pa_StartStream error: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    std::cerr << "Capturing from device '" << deviceInfo->name << "' "
              << "(" << channels << " channels, " << sampleRate << " Hz), framesPerBuffer=" << framesPerBuffer << "\n";
    std::cerr << "Press Ctrl+C to stop. Raw PCM (s16le) is written to stdout.\n";

    std::vector<int16_t> buffer(framesPerBuffer * static_cast<unsigned long>(channels));

	// capture loop
    while (!g_stop)
	{
        PaError r = Pa_ReadStream(stream, buffer.data(), framesPerBuffer);
        if (r == paNoError)
		{
            process_buffer(buffer.data(), framesPerBuffer, channels);
            continue;
        }
		else if (r == paInputOverflowed)
		{
            std::cerr << "Input overflow (samples dropped). Continuing...\n";
            // still try to continue
            continue;
        } else if (r == paTimedOut)
		{
            std::cerr << "Read timed out\n";
            continue;
        }
		else
		{
            std::cerr << "Pa_ReadStream error: " << Pa_GetErrorText(r) << "\n";
            break;
        }
    }

    std::cerr << "\nStopping capture...\n";
    err = Pa_StopStream(stream);
    if (err != paNoError) std::cerr << "Pa_StopStream error: " << Pa_GetErrorText(err) << "\n";

    err = Pa_CloseStream(stream);
    if (err != paNoError) std::cerr << "Pa_CloseStream error: " << Pa_GetErrorText(err) << "\n";

    Pa_Terminate();
    std::cerr << "Terminated.\n";
    return 0;
}