/**
 * A client for the live stats pipe of a Generals Online game: the frames the game sends while it runs (match data, live
 * statistics, events, the camera, the lobby, chat) and the one frame a reader may send back (a chat line).
 *
 * <h2>Where to start</h2>
 *
 * <ul>
 * <li>{@link com.generalsonline.stats.StatsPipe} connects, decodes, reconnects and delivers. Build it with
 *     {@link com.generalsonline.stats.StatsPipe#builder()}.</li>
 * <li>{@link com.generalsonline.stats.StatsListener} receives the frames, one method per frame type.</li>
 * <li>{@link com.generalsonline.stats.MatchState} keeps the game state for you (players joined with their match data,
 *     the light/full snapshot merge, the match lifecycle, the latest camera, recent events and chat) and calls a
 *     {@link com.generalsonline.stats.StateListener} when something changes.</li>
 * <li>{@link com.generalsonline.stats.DumpWriter} and {@link com.generalsonline.stats.DumpReader} record the raw frames
 *     and replay them into the same listener, paced by game time.</li>
 * <li>{@link com.generalsonline.stats.Format} has the small helpers every reader writes: game time, colours, display
 *     names and the text of an event.</li>
 * </ul>
 *
 * <h2>Threading</h2>
 *
 * <p>A pipe has one reader thread. Callbacks run on it unless
 * {@link com.generalsonline.stats.StatsPipe.Builder#executor(java.util.concurrent.Executor)} names another executor, for
 * example the UI thread. {@code MatchState} can be read from any thread and returns immutable values.
 *
 * <h2>Messages</h2>
 *
 * <p>The messages (package {@code com.generalsonline.stats.proto}) are generated from the game's protocol definition and
 * documented in {@code docs/stats-exporter.md} of the game repository.
 */
package com.generalsonline.stats;
