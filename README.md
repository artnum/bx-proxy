# bx-proxy

Read-only HTTP server for the MariaDB copy maintained by [bx-sync](../bx-sync). A GET uses the SQL row. One object is fetched from the Bexio API when the row is missing, `_deleted` is set, or `_last_updated` is at least `stale-seconds` old (default 7200). The response is then read back from the row just written. An unchanged payload still moves `_last_updated`, so the next GET inside the window is a hit.

Lists always come from SQL. `GET /2.0/company_profile` is the exception: that single row is refreshed when it is missing or stale. `business_years` and `vat_periods` are returned in full, without `limit`.

## Run

```
bx-proxy /usr/local/etc/bxsync.json
```

`mysql-host`, `mysql-user`, `mysql-password`, and `mysql-database` stay in that JSON file. Every other setting is read from the `_configuration` table when a row exists, and the row wins over the same name in the file. Names starting with `mysql-` are ignored. The table is created when it is missing.

bx-proxy also reads these keys from the file or from `_configuration`:

| Key | Default |
| --- | --- |
| `listen-host` | `127.0.0.1` |
| `listen-port` | `8080` |
| `stale-seconds` | `7200` |

`bxsync --login` stores the OAuth tokens in `_configuration` (`oauth-access-token`, `oauth-refresh-token`, `oauth-expires-at`). bx-proxy reloads those rows before each Bexio call and refreshes them when they are due. Login is not implemented here.

There is no proxy authentication. Keep the listen address on localhost.

## HTTP

Paths match Bexio, for example `GET /2.0/contact/123` and `GET /2.0/contact?limit=50&offset=0`. Query keys besides `limit` (default 500, max 500), `offset`, `page`, and `show_archived` are ignored. An explicit `offset` wins over `page`.

`Content-Type` is `application/json`. `X-Bx-Proxy` is `hit`, `refresh`, or `stale`. `stale` means Bexio could not be reached and an older row was returned. A Bexio 404 is HTTP 404 even when a row is still in SQL.

JSON is rebuilt from the row. Column names that start with `_` are omitted. Child routes such as `/2.0/kb_invoice/9/position` return only those rows. The parent's `_last_updated` decides whether the parent (and its children) are refreshed.

Anything other than GET is 405. An unknown path is 404. File bytes (`/3.0/files/{id}/download`) are not served. `GET /3.0/files/{id}` is the `bx_file` metadata row.

## Build

Debian `libwebsockets` 4.3.5 (`libwebsockets-dev`). The Makefile pins `PKG_CONFIG_LIBDIR=/usr/lib/x86_64-linux-gnu/pkgconfig` and puts `-I/usr/include` before other include paths so `/usr/local` is not selected.

```
make
make test
```

`vendor/bx-sync` is a snapshot of bx-sync `5a6dcad`. The proxy does not compile against a `../bx-sync` checkout. Before each Bexio call it re-reads `_configuration`, so a token written by bxsync is picked up. Two processes can still refresh the same token at the same moment.
