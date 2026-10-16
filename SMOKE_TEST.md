# Controlled-server smoke check

Use this checklist for a PR that changes live client behavior. It is a short
manual check, not a substitute for source tests. Run it only against a server
you control or are permitted to use with this experimental client. A public
SWGEmu login is not a CI target.

## Prepare

- Run the asset-free source suite with `python3 scripts/test.py`. If the
  change uses game data, run `python3 scripts/test.py --assets` with your own
  local installation. These checks need no server or original Windows client.
- For the live rows, supply your own controlled server, an existing ordinary
  test account and character, and a known safe outdoor starting area. A second
  account/character is useful for chat and remote movement. Use a building
  whose entry permissions you control if testing cells. Do not use admin
  commands or someone else's property just to satisfy this checklist.
- Record the PR commit and client build locally. Log both characters out before
  starting; keep other clients off those accounts. Note the original door
  permission locally if you will change it. Keep account details, server
  address, coordinates, character names, screenshots, packets, and raw logs
  off the PR.
- If you cannot meet a row's prerequisites, mark it **Not run** with a short
  reason. Do not infer a pass from an offline test.

| ID | Needs | Action | Expected observation |
| --- | --- | --- | --- |
| O1 | Source only | Run `python3 scripts/test.py`. | The source suite exits successfully; report the test count or failed test name. |
| O2 | Own local assets | Run `python3 scripts/test.py --assets` if the change touches asset loading, rendering, UI resources, or animation. | Tests exit successfully using only local owned inputs. |
| O3 | Mac app bundle, for runtime or packaging changes | After source tests, make the temporary build directory unavailable. Check the exact signed `.app` you intend to open contains its shader and library dependencies. | The bundle verifies and does not depend on files in the removed build directory. Record a live world load separately under S1 when a permitted server is available. |
| S1 | Controlled server, account A | Connect, choose the existing test character, and wait for the world. | One world load completes; the character and surrounding terrain appear, and the connection stays active during the short check. |
| S2 | Controlled server, account A | On open ground, walk a short route, stop, turn, and return to the starting spot. | Motion starts and stops, the visible endpoint agrees with the server after a pause, and there is no repeated snap-back or unexpected disconnect. |
| S3 | Controlled server, accounts A and B | Put both characters within local chat range. Send one brief Spatial message each way through the clients under test. | Each intended recipient sees one message in the expected tab; no duplicate or unrelated-room delivery. Do not copy message text into the PR. |
| S4 | Controlled server, account A and a benign owned/test object | Select the object and use its supported interaction once, if the changed feature has an interaction path. | The target/interaction feedback is visible and agrees with the server; no action repeats without input. State which interaction category was tested, without object identity. |
| S5a | Controlled server, account A and a building you control | Enter an allowed door, move within one cell, then exit. | Entry/exit updates the scene and movement cell; the character returns outside. |
| S5b | Controlled server, account A and a building you control | Deny entry only if you can safely change the door permission, then try once. | Denied entry leaves the character outside. Restore the prior permission afterward. |
| S6 | Controlled server, account A | Use Disconnect, reconnect once, select the same character, then disconnect again. | The first session ends cleanly, the next world load succeeds, and no stale entity or chat state carries over. |
| W1 | Optional original Windows client on the same controlled server | Repeat only the relevant S rows under comparable conditions. | Report whether the observed outcomes agree for the changed behavior; avoid claims of full visual or gameplay parity. |

The live rows are observational. A failed row is useful evidence; do not change
server state just to turn a failure into a pass. For a focused PR, run the
rows affected by the change and mark the rest **Not run**. Tests on different
server builds or unrelated scenes are not a controlled Windows comparison.

## Reset and report

After the check, leave the test building, restore any changed door permission,
return both characters to their safe starting area when practical, disconnect
both clients, and close the apps. If a connection fails mid-test, disconnect,
verify the previous session has ended, and restart at S1. Do not post
credentials or raw diagnostics while troubleshooting.

In the PR template, enter **Pass**, **Fail**, or **Not run** for each relevant
row. Give only aggregate evidence such as `2/2 zone-ins succeeded`,
`1/1 Spatial messages received`, or `0/1 denied entries blocked`. For a
failure, state the observed category and the local test's scope; keep identifying
details and raw records local. The owner can request a private follow-up if
more evidence is needed.
