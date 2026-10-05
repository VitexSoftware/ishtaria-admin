# ishtaria-admin

Text-mode (Turbo Vision) administration tool for an Ishtaria world server.
It works directly on the server's PostgreSQL database, so it needs no admin
HTTP API. Run it on the server host as the service user:

```sh
sudo -u ishtaria ishtaria-admin            # default: postgresql:///ishtaria?host=/var/run/postgresql
ishtaria-admin --database-url=postgresql:///other?host=/var/run/postgresql
```

| Menu (key) | Function |
| --- | --- |
| World maps (F2) | save the active map into the library, generate, load, rename, delete, export `.pgm`; when story datadisks are installed, *Generate map* offers a checkbox per disk to take it into account (stored with the map, applied on *Load*) |
| Players (F3) | rename, ban/unban (revokes sessions), delete (not possible with a permanent memorial) |
| Linked worlds (F4) | list portals; a link waiting for approval shows as `pending`: *Open* approves it, *Close* breaks it (the other world is told); create, disable, ban or delete portals |
| Server (F5) | schedule a shutdown with a delay and message, or cancel it |

Requires the server schema from migration `0013_admin.sql`, which `ishtaria-server`
applies at start-up. Loading a map replaces the active heightmap: restart the
server afterwards. Portals are only records for now; the server does not enforce
them until federation lands.

A scheduled shutdown is announced to clients as a non-dismissable `system`
message in `GET /world` (`messages[]`); the service then exits with status 0, so
systemd does not restart it.

## Build and test

```sh
cmake -S . -B build && cmake --build build
(cd build && ctest --output-on-failure)   # needs PostgreSQL and ../ishtaria-server/target/debug/ishtaria-server
dpkg-buildpackage -us -uc -b
```

English is the source language; Czech via gettext (`po/cs.po`). Error messages
produced by the database layer are English only.

Licence: AGPL-3.0-only.
