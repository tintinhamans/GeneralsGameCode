# Realtime protocol fixtures

Copies of `docs/realtime-fixtures/` from the GenOnline service repository, protocol v1 (service commit `4a8de745`). Each file is the single argument object of a hub message, as described in `docs/realtime-protocol.md` of the service.

The contract test parses every file with the client types in `Core/GameEngine/Include/GameNetwork/GenOnline/Realtime/RealtimeMessages.h` and serializes it back to the same JSON. A new fixture without a C++ type fails the test.

Refresh the copies when the service changes a message, in the same commit as the client change.
