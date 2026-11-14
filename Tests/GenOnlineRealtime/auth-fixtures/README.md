# Sign-in and session registration fixtures

Copies of the sign-in fixtures of the GenOnline service (identity phase 1, service commit `26d86f46`, client ids from service commit `cfb98836`). Same conventions as the realtime fixtures: snake_case, one JSON object per file.

| File | Content |
|---|---|
| `OAuthAuthorize.request.json` | query parameters of `GET /oauth/authorize`, opened in the system browser |
| `OAuthToken.code.request.json`, `OAuthToken.refresh.request.json` | form fields of `POST /oauth/token` |
| `OAuthToken.result.json`, `OAuthToken.error.json` | token response and OAuth error |
| `AuthSession.request.json`, `AuthSession.result.json`, `AuthSession.error.json` | body, 200 response and 400 error of `POST /api/v1/auth/session` |

The contract test builds the authorize query and the token forms with the client code and compares them with the request fixtures, and round-trips the response fixtures through the types in `Core/GameEngine/Include/GameNetwork/GenOnline/Realtime/RealtimeSignIn.h`. A new fixture without a check fails the test.
