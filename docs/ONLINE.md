# Playing with people outside your network

## What NexLevel does on its own

When you press **Host room**, NexLevel asks your router to forward the room port to your PC.
It uses UPnP, the same mechanism most online games and consoles use.

- No admin rights are needed, and nothing changes on your PC.
- The mapping is temporary. It is renewed every 30 minutes while the room is open, and removed
  when you close the room.
- The popup shows **Preparing...** for a few seconds. Then:
  - **Online: friends anywhere can join**. The code is 15 characters long and works from anywhere.
    Players on your own Wi-Fi connect over the local network automatically.
  - **Same Wi-Fi only: ...**. The 13-character code works only on your network.

## When online hosting is not possible

| Message | Cause | Fix |
|---|---|---|
| router has UPnP turned off | The router does not answer UPnP | Turn on UPnP in the router settings, or let another player host |
| your provider blocks hosting | The router itself is behind the provider's NAT (CGNAT, common on mobile and fibre ISPs) | Another player hosts, or wait for the NexLevel relay server (planned) |
| router refused to open a port | The router rejected the request | Another player hosts |

The first time you host, **Windows Firewall** may ask whether Geometry Dash can accept
connections. Choose **Allow**, or players will not get through.

## Testing your router

`tools\bot.ps1 host` opens a test room. It prints `UPnP OK: public ... port ...` and an ONLINE
code when your router supports it.
