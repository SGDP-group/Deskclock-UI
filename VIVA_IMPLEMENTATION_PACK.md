# Deskclock-UI Viva Master Pack

This document is your full viva preparation implementation pack for the project-owned code and directly used LVGL integration paths.

## 1) Scope and Coverage

### In scope (deep)
- main.c
- CMakeLists.txt
- src/device_config.c, src/device_config.h
- src/provisioning_service.c, src/provisioning_service.h
- src/doormount_service.c, src/doormount_service.h
- src/home_api_client.c, src/home_api_client.h
- src/home_config.h
- src/net_stream.c, src/net_stream.h
- src/focus_image_stream.c, src/focus_image_stream.h
- src/focus_camera_capture.c, src/focus_camera_capture.h
- lvgl/src/ui.c, lvgl/src/ui.h
- lvgl/src/screens/screen_home.c, lvgl/src/screens/screen_home.h
- lvgl/src/screens/screen_focus_session.c, lvgl/src/screens/screen_focus_session.h
- lvgl/src/screens/screen_camera_preview.c, lvgl/src/screens/screen_camera_preview.h
- lvgl/src/components/session_confirm_popup.c, lvgl/src/components/session_confirm_popup.h
- lvgl/src/data/app_state.c, lvgl/src/data/app_state.h
- lvgl/src/data/ring_buffer.c, lvgl/src/data/ring_buffer.h
- lvgl/src/data/storage.c, lvgl/src/data/storage.h
- lvgl/src/styles/theme.c, lvgl/src/styles/theme.h

### Reference only (not deep)
- Broad vendored LVGL internals outside directly used app integration files
- LVGL demos/examples internals

---

## 2) Full Architecture Explanation

### 2.1 What this product is
Deskclock-UI is a focus-session desk display application built with LVGL.
It supports:
- Provisioning a device onto Wi-Fi using a SoftAP flow
- Fetching due tasks from a backend
- Running quick or task-based focus sessions
- Capturing camera frames and streaming them over a socket pipeline
- Doormount setup workflow from UI settings

### 2.2 End-to-end startup flow
1. main.c registers crash handling and LVGL log callback.
2. Device config is loaded from disk (user_id, provisioning state, Wi-Fi creds).
3. If not provisioned, provisioning service starts SoftAP and local HTTP server.
4. LVGL initializes display and input backend.
   - Windows path uses SDL window and mouse
   - Linux path uses framebuffer and evdev touch
5. UI route decision:
   - Unprovisioned: provisioning QR/instructions screen
   - Provisioned: home screen
6. Main event loop runs forever:
   - lv_timer_handler()
   - sleep 5 ms
   - checks if provisioning just finished and then switches to home UI

### 2.3 Runtime subsystems
- UI subsystem (LVGL custom files under lvgl/src)
- Device config subsystem (persistent state)
- Provisioning subsystem (SoftAP + HTTP endpoint)
- Task API subsystem (GET due-today + PATCH status)
- Camera capture subsystem (V4L2, MJPEG or YUYV)
- Stream transport subsystem (queue + socket worker)
- Image framing subsystem (metadata + binary packet)

### 2.4 Data flow during focus session
1. User starts task or quick session from home UI.
2. screen_focus_session creates stream session metadata.
3. focus_image_stream starts net_stream transport.
4. focus_camera_capture worker acquires frames.
5. If YUYV, frame is converted and encoded to JPEG.
6. focus_image_stream wraps frame with header metadata.
7. net_stream enqueues and background worker sends over TCP.
8. Diagnostics are reflected in status text on session screen.

### 2.5 Key platform split
- Windows:
  - UI works via SDL
  - provisioning/doormount/camera/stream transport largely stubbed or unsupported
- Linux:
  - Full runtime path including camera, Wi-Fi setup, and networking

### 2.6 Build and linking model
- Top-level CMake builds one executable.
- App source files from src are linked with lvgl::lvgl.
- Windows links SDL2, ws2_32, dbghelp.
- Linux links JPEG and pthread stack through Threads package.

---

## 3) File-by-File Deep Explanation

### 3.1 main.c
- Role: process entrypoint and orchestration.
- Responsibilities:
  - crash logging setup
  - LVGL log sink
  - load config and optionally start provisioning service
  - initialize display and input backend
  - route to provisioning UI or home UI
  - run main LVGL loop
- Viva angle:
  - Why lv_timer_handler interval matters
  - Why provisioning transition check exists inside loop
  - Why display/input backend is platform conditional

### 3.2 src/device_config.c and src/device_config.h
- Role: persistent device profile.
- Data model: provisioned, user_id, wifi_ssid, wifi_password.
- Storage:
  - Linux: /etc/deskclock/device_config.json
  - Windows: local json file
- Important logic:
  - default config with user_id fallback
  - minimal hand-rolled JSON parsing helpers
  - atomic save with temp file rename
  - factory reset zeros sensitive fields and de-provisions
- Viva angle:
  - Why no full JSON library was used
  - tradeoff of simplistic parser vs robustness

### 3.3 src/provisioning_service.c and src/provisioning_service.h
- Role: bring new device online.
- Workflow:
  - create random SoftAP SSID
  - configure AP with nmcli
  - start local HTTP server on port 8080
  - accept POST /api/provision payload
  - save Wi-Fi and user_id
  - tear down AP and join home Wi-Fi
  - set provisioned true if station join succeeds
- Security checks:
  - basic shell-unsafe character filtering before system() calls
- Viva angle:
  - why delayed apply flag is used after HTTP accept
  - failure handling if connect to home Wi-Fi fails

### 3.4 src/doormount_service.c and src/doormount_service.h
- Role: setup a DoorMount companion device.
- Functions:
  - scan for DoorMount-* SSIDs
  - connect to selected open AP
  - send setup payload with home Wi-Fi and user_id
  - reconnect device back to home Wi-Fi
- Viva angle:
  - Why reconnect step is mandatory
  - risk of command-line credential exposure with nmcli

### 3.5 src/home_api_client.c and src/home_api_client.h
- Role: backend HTTP client for tasks.
- Functions:
  - fetch due-today tasks
  - patch subtask statuses (pending/in-progress/completed)
- Parsing:
  - manual JSON extraction from response body
  - maps API fields into HomeApiTask
- Time handling:
  - builds query with local date, time, epoch, tz offset
- Viva angle:
  - how timezone offset is calculated per platform
  - why parsing should check HTTP status line

### 3.6 src/home_config.h
- Role: central runtime constants.
- Holds API host/port, stream host/port, FPS, session durations.
- Notable dev setting:
  - countdown speed multiplier can accelerate timer for testing.
- Viva angle:
  - impact of compile-time constants on flexibility

### 3.7 src/net_stream.c and src/net_stream.h
- Role: async transport worker for frame packets.
- Design:
  - fixed-size circular queue
  - producer API net_stream_enqueue
  - consumer thread sends over TCP
  - reconnect on failure with backoff
- Diagnostics:
  - connected state
  - queue depth
  - fail streak and mode
- Viva angle:
  - queue overflow behavior and backpressure tradeoff
  - why transport is decoupled from camera capture thread

### 3.8 src/focus_image_stream.c and src/focus_image_stream.h
- Role: packet framing around JPEG payloads.
- Adds metadata header:
  - stream type
  - user/session/subtask identity
  - timestamp and sequence
- Exposes:
  - start task stream
  - start quick stream
  - pause/resume
  - send one JPEG frame
  - stats snapshot
- Viva angle:
  - protocol design choices (binary shell + JSON metadata)

### 3.9 src/focus_camera_capture.c and src/focus_camera_capture.h
- Role: camera acquisition and frame production.
- Linux path details:
  - probes multiple /dev/video candidates
  - negotiates MJPEG first, fallback YUYV
  - mmap buffers, stream on, select loop, dqbuf/qbuf cycle
  - if YUYV, convert and JPEG encode
  - optional preview frame copy in RGB565
  - rate-limit frame publishing
- Recovery:
  - handles repeated timeouts with camera reinitialize path
- Viva angle:
  - full V4L2 lifecycle explanation
  - thread-safe preview buffer access and worker lifecycle

### 3.10 lvgl/src/ui.c and lvgl/src/ui.h
- Role: UI routing and top-level screen transitions.
- Routes:
  - provisioning screen
  - home
  - camera preview
  - focus session
- Viva angle:
  - why dedicated route helpers improve clarity and testability

### 3.11 lvgl/src/screens/screen_home.c and .h
- Role: home dashboard.
- Features:
  - clock/date labels
  - pull-to-refresh gesture on time area
  - periodic API refresh timer
  - task card pool rendering
  - quick-focus entry
  - settings popup and reset flow
  - doormount setup popup and async worker polling
- Viva angle:
  - event-driven LVGL design
  - separation of fetch state vs render state

### 3.12 lvgl/src/screens/screen_camera_preview.c and .h
- Role: pre-session framing check.
- Features:
  - starts preview capture mode
  - timer pulls latest RGB565 frame copy
  - canvas invalidation for live preview
  - go-back and start buttons
- Viva angle:
  - why preview runs with stream disabled

### 3.13 lvgl/src/screens/screen_focus_session.c and .h
- Role: active focus/break session controller.
- Features:
  - countdown arc and timer
  - pause/resume/stop controls
  - break popup and completion logic
  - quick session and task chunk progression
  - backend status updates for task lifecycle
  - runtime diagnostics from camera and stream subsystems
- Viva angle:
  - phase machine logic and transition correctness

### 3.14 lvgl/src/components/session_confirm_popup.c and .h
- Role: reusable modal for starting sessions.
- Features:
  - quick or task mode
  - callback interface back into home screen
  - entry/exit animations
- Viva angle:
  - reusable component boundary design in LVGL C project

### 3.15 lvgl/src/data/app_state.c and .h
- Role: shared mutable state and UI references.
- Pattern:
  - global app state struct
  - setter functions update state and bound widgets
- Viva angle:
  - benefits and limits of singleton state in embedded UI

### 3.16 lvgl/src/data/ring_buffer.c and .h
- Role: simple fixed circular buffer utility.
- Viva angle:
  - overwrite-on-full semantics and deterministic memory behavior

### 3.17 lvgl/src/data/storage.c and .h
- Role: tiny key-value persistence file helper.
- Behavior:
  - append writes
  - last occurrence wins on read
- Viva angle:
  - complexity vs reliability tradeoff

### 3.18 lvgl/src/styles/theme.c and .h
- Role: central style declarations and apply helpers.
- Viva angle:
  - theming consistency and style reuse strategy

---

## 4) Viva Question Bank With Model Answers

### A) System architecture and build

Q1. Explain the app startup sequence from process launch to first screen.
A1. The app sets crash and LVGL logging handlers, loads persisted device config, conditionally starts provisioning service if not provisioned, initializes LVGL display/input, then chooses provisioning screen or home screen before entering the continuous lv_timer_handler loop.

Q2. Why does the main loop keep checking provisioning-screen state and config status?
A2. Provisioning completes asynchronously in a background thread, so the main loop must detect state transition and route from provisioning UI to home without restarting process.

Q3. What are the two primary platform targets and how do they differ?
A3. Windows targets SDL desktop visualization and uses multiple stubs for hardware/network services. Linux target is deployment path with framebuffer/evdev, camera capture, nmcli provisioning, and streaming.

Q4. Why is one executable used rather than multiple processes?
A4. Single-process architecture simplifies shared state and LVGL event loop coordination, reduces IPC complexity, and is suitable for constrained embedded deployment.

Q5. Why are many LVGL features set via compile definitions?
A5. Compile definitions provide deterministic feature flags and memory/performance control at build time without runtime config dependency.

Q6. What is the role of CMake in this project?
A6. CMake centralizes source selection, platform-specific compile options, and link dependencies so the same codebase can build on Windows and Linux with conditional backends.

Q7. Why is JPEG linked only on Linux path?
A7. JPEG encode path is used by Linux camera fallback from YUYV to JPEG. Windows currently uses stubs for capture pipeline so libjpeg link is unnecessary there.

Q8. What happens if provisioning service cannot start SoftAP?
A8. provisioning_service_start_if_needed returns false, UI can still show provisioning guidance, and logs indicate failure reason. Device remains unprovisioned until service can be restarted.

### B) Device config and provisioning

Q9. What fields are persisted in device config?
A9. Provisioned flag, user_id, wifi_ssid, and wifi_password.

Q10. Why does device_config_save write to tmp then rename?
A10. This makes persistence more atomic, reducing risk of partial file corruption if crash or power loss happens during write.

Q11. Why does factory reset set user_id to zero?
A11. It intentionally invalidates user association so provisioning flow can request and save fresh identity and credentials.

Q12. How does provisioning HTTP payload validation work?
A12. It checks required fields wifiSsid, wifiPassword, userId and validates lengths/ranges before saving pending config.

Q13. Why is provisioning apply delayed after POST response?
A13. The service first acknowledges payload quickly, then applies network reconfiguration in service loop to avoid long blocking in client handler.

Q14. What endpoints does provisioning service expose?
A14. GET /health and POST /api/provision.

Q15. Why is shell character filtering used for nmcli commands?
A15. Since credentials are interpolated into system() command strings, filtering reduces command injection risk.

Q16. What is still risky even with shell filtering?
A16. Credentials can still appear in process command lines and logs, and filter may miss edge characters; safer alternatives are exec-family argument vectors or DBus APIs.

### C) Home API and task state

Q17. How are due-today requests parameterized?
A17. Query includes userId, deviceDate, deviceTime, deviceEpoch, and tzOffsetMinutes for backend time-aware task resolution.

Q18. What is mapped from API into UI task model?
A18. id, completion, duration, title/name fallback, description fallback, status text, and formatted time range.

Q19. Why is there a fallback duration in parser?
A19. To prevent zero or invalid duration from breaking session flow; parser defaults to 30 minutes.

Q20. How is status update to backend done when session starts?
A20. Focus screen marks subtask in-progress using PATCH endpoint before or during session initialization.

Q21. How is completion reported?
A21. On completion popup confirmation for task sessions, it sends mark_subtask_completed.

Q22. Why can subtask be marked pending on early stop?
A22. If user exits before completion, state can revert to pending so task remains actionable in backend workflow.

Q23. What is a parser limitation in home_api_client?
A23. Hand-rolled string search parser is fragile for nested structures, escaping, and field order variability.

Q24. What reliability improvement would you add first?
A24. Explicit HTTP status parsing and retries with bounded backoff for transient network failures.

### D) Streaming and camera pipeline

Q25. Why is focus_image_stream separate from net_stream?
A25. focus_image_stream handles application framing metadata, while net_stream handles transport queueing/reconnect. This clean separation simplifies maintenance and debugging.

Q26. Describe the packet format at high level.
A26. A fixed binary prefix with magic/version/lengths followed by JSON metadata header and JPEG payload.

Q27. Why use sequence numbers in frame metadata?
A27. Receiver can detect dropped or reordered frames and correlate diagnostics.

Q28. What does net_stream queue depth represent?
A28. Number of pending unsent frame packets buffered between producer and socket worker.

Q29. What happens when queue is full?
A29. net_stream_enqueue returns false and frame gets rejected by upstream, preventing producer from blocking indefinitely.

Q30. Why is reconnect logic in worker thread, not producer thread?
A30. Producer should stay focused on capture pacing; worker owns socket state and retries asynchronously.

Q31. Why prefer MJPEG camera mode when available?
A31. MJPEG avoids CPU-heavy per-frame encode and reduces bandwidth overhead from raw YUYV.

Q32. Why does YUYV path need JPEG scratch buffer?
A32. Each YUYV frame must be converted and compressed before sending, so a buffer is required for encoded output.

Q33. Why does capture worker rate-limit publishes?
A33. It enforces target stream FPS to balance CPU/network use and avoid flooding downstream receiver.

Q34. What triggers camera pipeline reinitialization?
A34. Repeated timeout failures crossing threshold trigger stream-off, close, and re-open sequence.

Q35. Why is preview copy protected by mutex?
A35. Worker writes latest preview while UI reads it; mutex prevents torn reads and buffer corruption.

Q36. Why does preview mode disable stream send?
A36. It lets user frame camera without producing network load or backend session traffic.

### E) UI and LVGL behavior

Q37. Where are high-level UI routes managed?
A37. In ui.c through helper functions that create and load specific screens.

Q38. How is pull-to-refresh implemented on home screen?
A38. It tracks press delta on clock area events, updates spinner position/opacity, and triggers fetch when drag threshold and cooldown are satisfied.

Q39. Why does home screen use a fixed card pool?
A39. Fixed pool reduces dynamic object churn and gives predictable memory usage.

Q40. How are task start actions dispatched?
A40. Each card start surface has LV_EVENT_CLICKED callback with task index in user data; callback opens confirmation popup.

Q41. Why use a separate session confirmation component?
A41. Reusability and consistency: same animation and UX for quick and task starts with callback-based integration.

Q42. Explain focus session phase machine.
A42. It transitions between focus, break, and waiting-popup states. Countdown callback behavior depends on current phase and pause state.

Q43. How does pause affect camera and stream?
A43. Pause toggles both camera capture paused flag and image stream paused flag so transport and diagnostics reflect session state.

Q44. Why are diagnostics shown in focus screen status label?
A44. It exposes runtime health (camera/link/send/queue) for support and debugging without external tools.

Q45. How does camera preview transition to focus screen?
A45. Start button stops preview worker and navigates with preserved title, duration, quick/task mode, and task id.

Q46. What is the app_state design pattern?
A46. Singleton global state with setter functions that update both data and bound widgets, simplifying cross-screen updates.

### F) Reliability, security, and design tradeoffs

Q47. Identify one race-risk area and mitigation.
A47. Stream/session globals can be accessed by multiple contexts. Add mutex around mutable stream state and unify ownership boundaries.

Q48. Identify one security concern and mitigation.
A48. Plain-text credential handling in command strings and config file. Mitigate with secure secret storage, restricted file permissions, and process-safe command invocation.

Q49. Identify one maintainability issue and mitigation.
A49. Manual JSON parsing repeated in multiple files. Mitigate by central parser utility or lightweight JSON library wrapper.

Q50. What is the impact of compile-time hardcoded hosts?
A50. Deployment inflexibility and environment lock-in. Better approach is runtime config loaded from secure storage.

Q51. Why is bounded queue better than unbounded queue here?
A51. Embedded systems require deterministic memory ceilings; bounded queue prevents memory blowup under network degradation.

Q52. What testing gap is most critical?
A52. App-level unit/integration tests for provisioning, API parser, stream framing, and camera lifecycle are missing and should be prioritized.

---

## 5) Coding Drills With Answer Guidance

### Drill 1: Add HTTP status validation to due-today fetch
Task:
- Ensure success only when response status is 2xx.

Expected approach:
1. Parse first response line.
2. Reject non-2xx before body parsing.
3. Propagate clear error status.

Answer sketch:
~~~c
static bool http_status_2xx(const char *res) {
    if (res == NULL) return false;
    return (strncmp(res, "HTTP/1.1 2", 10) == 0) ||
           (strncmp(res, "HTTP/1.0 2", 10) == 0);
}

// inside http_fetch_due_today after recv
if (!http_status_2xx(response)) {
    return false;
}
~~~

### Drill 2: Escape JSON special chars before writing config strings
Task:
- Prevent malformed JSON when SSID/password contain quotes or backslashes.

Expected approach:
1. Add helper to escape \ and ".
2. Use escaped buffers in fprintf JSON template.

Answer sketch:
~~~c
static void json_escape(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] != '\0' && j + 2 < cap; i++) {
        char c = in[i];
        if (c == '\\' || c == '"') out[j++] = '\\';
        out[j++] = c;
    }
    out[j] = '\0';
}
~~~

### Drill 3: Add queue full counter to net_stream diagnostics
Task:
- Expose how often enqueue fails due to full queue.

Expected approach:
1. Add static counter increment on queue full return.
2. Add getter in header.
3. Surface in focus diagnostics label.

### Drill 4: Make focus_image_stream state thread-safe
Task:
- Guard stream state and counters with mutex.

Expected approach:
1. Add static pthread_mutex_t.
2. Lock in start/stop/send/stats functions.
3. Keep lock scope minimal around shared state.

### Drill 5: Add reconnect backoff cap in net_stream
Task:
- Avoid excessive reconnect churn or too-fast loops.

Expected approach:
1. Keep backoff base and max.
2. Increase wait with fail streak up to max cap.

### Drill 6: Improve provisioning command safety
Task:
- Replace system command string interpolation with safer invocation path.

Expected approach:
1. Use execvp or posix_spawn with argv list.
2. Avoid passing credentials in raw shell command string.

### Drill 7: Add runtime host override from storage
Task:
- Allow stream host override without recompiling.

Expected approach:
1. Read optional host key from storage at startup.
2. Fallback to HOME_GAZE_STREAM_HOST if missing.

### Drill 8: Add API retry for transient network error
Task:
- Retry due-today fetch up to 3 times with short delay.

Expected approach:
1. Loop with bounded attempts.
2. Sleep between attempts.
3. Stop on first success.

### Drill 9: Add explicit camera state label in preview screen
Task:
- Show Starting, Live, Stalled, Error statuses.

Expected approach:
1. Track last sequence change timestamp.
2. If stale beyond threshold, show Stalled.

### Drill 10: Fix include path consistency in main.c
Task:
- Ensure UI header include path matches actual location and build include dirs.

Expected approach:
1. Use a single canonical include path for UI header.
2. Align with include_directories and source layout.

---

## 6) Mock Viva Script (Ask + Model Answer)

Use this as oral rehearsal.

1) Ask: Why split transport queue from camera capture?
Model answer: It keeps capture pacing independent from network jitter and enables bounded buffering plus reconnect logic in one dedicated worker.

2) Ask: How does app recover from camera timeouts?
Model answer: Consecutive timeout detection triggers stream-off, close, and pipeline reinitialize, then camera_ready is restored if successful.

3) Ask: What are the main states in focus session controller?
Model answer: Focus, Break, and Waiting Popup. Timer callback and controls act differently per state.

4) Ask: Why does quick session use generated session key?
Model answer: It creates unique backend correlation id for non-task sessions while still associating user identity.

5) Ask: What happens if network queue fills up?
Model answer: New frame enqueue is rejected, frame is dropped, and diagnostics reflect rejected count and queue depth.

6) Ask: Why is pull-to-refresh cooldown needed?
Model answer: It avoids accidental repeated fetch bursts and stabilizes UX under rapid gestures.

7) Ask: Why use fixed card pool instead of dynamic cards per fetch?
Model answer: Predictable memory footprint and reduced object lifecycle overhead are better for embedded GUI performance.

8) Ask: How is device provisioning persisted safely?
Model answer: Config is written to temp file then renamed atomically, reducing partial-write corruption risk.

9) Ask: What is one major parser risk in this codebase?
Model answer: Hand-rolled JSON extraction can fail with escaped characters or unexpected structure, causing silent data errors.

10) Ask: What immediate production hardening would you prioritize?
Model answer: Safer command execution for provisioning, stronger parser robustness, and app-level tests for API and camera pipeline.

---

## 7) Final Study Checklist

### Architecture readiness
- You can narrate boot flow in under 90 seconds.
- You can draw subsystem boundaries from memory.
- You can explain Linux vs Windows behavior differences.

### Code readiness
- You know every public API in src headers.
- You can explain focus session state transitions correctly.
- You can explain V4L2 capture loop and queueing model.

### Viva defense readiness
- You can justify major design tradeoffs.
- You can identify at least 5 realistic risks and mitigations.
- You can propose next engineering improvements with priority order.

### Coding round readiness
- You can implement one reliability fix quickly.
- You can implement one parser hardening fix safely.
- You can explain test strategy for network and camera modules.

---

## 8) What to Practice Next

1. Repeat architecture explanation until it is fluent and time-bounded.
2. Answer Q1 to Q52 aloud without reading model answers.
3. Implement Drill 1 and Drill 3 in code as timed exercises.
4. Run a 20-minute mock viva with random question selection.
