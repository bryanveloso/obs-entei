# Entei Caption Provider for OBS Studio

## Introduction

The Entei Caption Provider is a plugin that receives real-time transcriptions via WebSocket and sends them to OBS's native caption system for streaming platforms like Twitch and YouTube.

**Entei** is an alliteration of **NTI** (Network Transcript Interface), inspired by NDI (Network Device Interface).

## Features

* Receives transcriptions from a WebSocket server
* Sends captions as CEA-708 metadata (not rendered in video)
* Auto-starts when streaming/recording begins
* Automatic reconnection on connection loss
* Configurable WebSocket URL and settings
* Shows [CC] button on streaming platforms for viewers

## Installation

1. Download the latest release for your platform
2. Extract to your OBS plugins folder:
   - **Windows**: `C:\Program Files\obs-studio\obs-plugins\64bit`
   - **macOS**: `~/Library/Application Support/obs-studio/plugins`
   - **Linux**: `/usr/share/obs/obs-plugins`
3. Restart OBS Studio
4. Access via Tools → Entei Caption Provider

## Configuration

1. Open Tools → Entei Caption Provider
2. Configure settings:
   - **WebSocket URL**: Default `ws://localhost:8889/events`
   - **Reconnect Delay**: Seconds between reconnection attempts
   - **Show Partial Captions**: Display in-progress transcriptions

## WebSocket Protocol

The plugin expects JSON messages in this format:

```json
{
  "type": "audio:transcription",
  "timestamp": 1703001234567,
  "text": "Hello world",
  "is_final": true
}
```

* `type`: Must be `"audio:transcription"`
* `timestamp`: Unix timestamp in milliseconds
* `text`: The caption text to display
* `is_final`: Optional boolean (default: true). If false, caption is partial

## Building from Source

### Requirements

* OBS Studio 31.0.0 or later
* CMake 3.28 or later
* Jansson library (optional, for enhanced JSON parsing)
* Platform-specific build tools (see below)

### macOS

```bash
brew install jansson
cmake --preset macos
cmake --build build_macos --config Release
```

### Windows

```bash
cmake --preset windows-x64
cmake --build build_windows_x64 --config Release
```

### Linux

```bash
sudo apt-get install libjansson-dev
cmake --preset linux-x86_64
cmake --build build_linux_x86_64 --config Release
```

## License

This project is licensed under the GPL-2.0 License - see the [LICENSE](LICENSE) file for details.

## Credits

Based on the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate)