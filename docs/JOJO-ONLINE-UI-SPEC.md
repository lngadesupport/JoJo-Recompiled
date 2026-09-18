# JoJo Recompiled — Online UI / Matchmaking Specification

Status: approved visual/interaction target
Reference: six user-provided screenshots in the design conversation.

## Product goal

Add an ONLINE flow to JOJO Recompiled that mirrors the reference navigation and composition while using JoJo Recompiled branding and original assets.

## Required screens

1. **Online Home**
   - player name field;
   - region selector;
   - CUSTOM GAME;
   - MATCHMAKING > CASUAL;
   - MATCHMAKING > RANKED;
   - HOW TO PLAY ONLINE;
   - back and settings affordances.

2. **Lobby**
   - blue/orange team bars;
   - player slots;
   - empty slots marked with +;
   - spectator slot/count;
   - CHANGE SLOT;
   - READY checkbox/state;
   - chat panel and text entry;
   - host-only START button;
   - close/leave button.

3. **Public Servers**
   - title and blue/orange header bars;
   - scrollable room list;
   - online/status indicator;
   - room name;
   - current/max players;
   - selected room details/password;
   - HOST;
   - CONNECT;
   - refresh.

4. **Create Lobby**
   - lobby name;
   - max players;
   - privacy: PUBLIC / PRIVATE;
   - optional password;
   - HOST;
   - back.

5. **Find Match**
   - centered modal;
   - player count (2 players);
   - CASUAL / RANKED queue selection;
   - CURRENT REGION;
   - FIND button;
   - close/cancel.

6. **Searching Opponent / Connecting**
   - large centered status;
   - animated activity indicator;
   - Connecting / Searching text;
   - back/cancel;
   - transition into Lobby when a peer/session is established.

## Architecture boundary

The existing OnlineSessionController / DirectUdpSession remains the transport lifecycle for direct peer sessions.

The new lobby/matchmaking model must not pretend a central service exists. Public room discovery, ranked/casual global matchmaking, NAT traversal and relay require a directory/matchmaking service. The client UI and service boundary may be implemented before that backend is deployed.

## Visual rules

- near-black textured/grunge background;
- sharp white outlines;
- blue left/player-one accents and orange right/player-two accents;
- condensed bold uppercase typography;
- red close buttons;
- large negative space, matching the references;
- mouse, keyboard and controller navigation;
- no emulator-facing terminology.

## Main launcher integration

Main menu becomes:

1. START GAME
2. ONLINE
3. CONTROLS
4. SETTINGS
5. EXIT

ONLINE opens Online Home. Escape/back returns to the launcher main menu.

## Networking requirements

- direct host/join continues to use existing UDP session controller;
- rollback remains the gameplay synchronization foundation;
- lobby/matchmaking frontend is transport-agnostic;
- service-delivered rooms must include a connectable endpoint/token rather than trusting display names;
- ranked queue must not be labeled operational until server-authoritative matchmaking exists;
- passwords must never be persisted as plaintext in settings.

## Shipping rule

A button that requires the central matchmaking service must either work against a configured service or clearly report service unavailable; it must never fabricate opponents or public rooms.