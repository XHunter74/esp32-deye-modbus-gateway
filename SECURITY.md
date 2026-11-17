# Security Policy

## Reporting a vulnerability

Please do **not** open a public issue for security problems.

Report them privately through GitHub: **Security → Report a vulnerability** on this repository
(private vulnerability reporting is enabled). You will get an answer as soon as possible.

## Scope

This firmware runs on a local network and talks to a solar inverter. Relevant reports include:

- ways to change gateway settings or flash firmware without the web password;
- ways to crash or hang the gateway over the network (TCP bridge, web UI);
- ways to send unintended write commands to the inverter.

## Hardening notes for users

- Set a web password in **Settings**; without it anyone on your LAN can change settings and upload firmware.
- The Modbus TCP port (8899/502) has no authentication by design. Anyone who can reach it can read and **write**
  inverter registers. Keep the gateway on a trusted network or a separate VLAN, and never expose it to the internet.
