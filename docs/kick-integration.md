# Kick.tv Integration Guide

This guide explains how to use the Kick.tv chat integration in Chatterino7.

## Overview

Chatterino7 now supports viewing and sending messages to Kick.tv channels, as well as merging Twitch and Kick chats into a single unified view.

## Enabling Kick Integration

1. Open **Settings** (click the gear icon or press `Ctrl+P`)
2. Navigate to **General** settings
3. Scroll down to the **Kick Integration** section
4. Check **Enable Kick integration**
5. Restart Chatterino7 for changes to fully take effect

## Logging into Kick

1. Ensure Kick integration is enabled (see above)
2. In the **Kick Integration** section of General settings, click **Login with Kick**
3. Your browser will open to Kick's authorization page
4. Log in to your Kick account and authorize Chatterino7
5. The browser will show "Authentication successful" when complete
6. Your Kick login status will appear in the settings

## Adding a Kick Channel

1. Click the **+** button to add a new split or use **Change channel** from the context menu
2. In the platform dropdown, select **Kick**
3. Enter the channel name (e.g., `xqc`) or full URL (e.g., `https://kick.com/xqc`)
4. Press Enter or click OK

The channel will connect and display "Connected" in the tab header.

## Merging Twitch and Kick Channels

The merge feature allows you to combine chats from both platforms into a single view. This is useful when a streamer broadcasts to both Twitch and Kick simultaneously.

### How to Merge

1. Open a Twitch or Kick channel
2. Right-click the tab header to open the context menu
3. Select **Merge with...**
4. Choose the target platform (Twitch or Kick)
5. Enter the channel name to merge with
6. Click **Merge**

A new merged view will open showing messages from both platforms with platform indicators (`[T]` for Twitch, `[K]` for Kick).

### Unmerging Channels

1. Right-click the merged channel's tab header
2. Select **Unmerge channels**
3. Both channels will be opened as separate splits

## Sending Messages in Merged View

When viewing a merged channel, you can choose where to send your messages:

1. Look for the platform selection buttons above the message input:

   - **Both** - Send to both Twitch and Kick
   - **Twitch** - Send only to Twitch
   - **Kick** - Send only to Kick

2. Select your preference and type your message
3. The selected button will be highlighted

### Send Status Indicators

- `T✓` - Message sent to Twitch successfully
- `K✓` - Message sent to Kick successfully
- `T✗` - Failed to send to Twitch
- `K✗` - Failed to send to Kick

If a send fails, an error message will appear in chat explaining the reason.

## Known Limitations

- **Chat History**: Kick does not provide a public API for chat history. When you join a Kick channel, you'll only see live messages going forward. A notice will appear: "📡 Kick: Showing live messages only."

- **Emotes**: Native Kick emotes are displayed. Third-party emotes (7TV, BTTV, FFZ) work for channels that have them configured.

- **Moderation**: Moderator actions in Kick channels are not yet fully supported.

## Troubleshooting

### "Kick integration is disabled" error

Enable Kick integration in Settings → General → Kick Integration.

### Can't send messages to Kick

1. Ensure you're logged into Kick (check Settings → General → Kick Integration)
2. Verify your access hasn't expired (try logging in again)
3. Check that you're not rate-limited (Kick allows 3 messages per second)

### Channel shows "Connection Failed"

1. Check your internet connection
2. Verify the channel name is correct
3. Try reconnecting via the context menu

### Messages not appearing

1. Check that the channel is connected (look for "[Connected]" in the header)
2. Ensure the chat is active on Kick.com
3. Try disconnecting and reconnecting

## Technical Details

### Authentication

Kick authentication uses OAuth 2.1 with PKCE for secure browser-based login. Your credentials are stored locally and encrypted.

### Real-time Messaging

Kick chat uses a Pusher-compatible WebSocket protocol for real-time message delivery.

### Rate Limits

- Kick limits chat messages to approximately 3 per second
- Exceeding this limit will result in a "Rate limited" error
- Wait for the countdown to complete before sending more messages

---

For bug reports or feature requests, please visit the [Chatterino7 GitHub repository](https://github.com/SevenTV/chatterino7).
