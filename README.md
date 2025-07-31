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
