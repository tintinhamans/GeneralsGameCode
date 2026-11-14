# REST fixtures

Copies of `docs/api-fixtures/` from the GenOnline service repository, protocol v1 (service commit `2a368c5d`). Each file is a request or success body of a REST endpoint that replaces an earlier hub method, as listed in section 10 of `docs/realtime-protocol.md` of the service. `error.response.json` is a failure body with the code and params of section 7.

The contract test parses every file with the client types in `Core/GameEngine/Include/GameNetwork/GenOnline/Realtime/RealtimeRest.h` and serializes it back to the same JSON. A new fixture without a C++ type fails the test.

Refresh the copies when the service changes a body, in the same commit as the client change.
