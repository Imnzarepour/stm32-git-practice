#ifndef INFRA_EC200U_MANAGER_H
#define INFRA_EC200U_MANAGER_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/ec200u-manager.md
 * TODO(open-item): این Contract هنوز پیش‌نویس است، نه Final (OPEN-ITEMS.md 🔴 #1).
 * GPS/Server/SMS/Call Management باید فقط از طریق این Contract با EC200U کار کنند؛
 * هرگز AT Command/PDP/Socket/GNSS Protocol/URC خام را در Management نبینید.
 * این لایه معنای محصولی داده را نمی‌داند (مثلاً "Position" اینجا فقط مختصات خام است،
 * نه "Vehicle Position" که معنای آن متعلق به GPS Management است). */

typedef enum {
    EC200U_OK,
    EC200U_BUSY,
    EC200U_NOT_READY,
    EC200U_ERROR
} EC200U_Result;

typedef enum {
    EC200U_MODULE_OFF,
    EC200U_MODULE_INITIALIZING,
    EC200U_MODULE_READY,
    EC200U_MODULE_DEGRADED,
    EC200U_MODULE_ERROR
} EC200U_ModuleState;

typedef enum {
    EC200U_NETWORK_UNKNOWN,
    EC200U_NETWORK_SEARCHING,
    EC200U_NETWORK_REGISTERED,
    EC200U_NETWORK_LOST
} EC200U_NetworkState;

/* --- Module lifecycle --- */
EC200U_Result      EC200U_Init(void);
void                EC200U_Process(void); /* یک گام کوچک AT/URC Handling در هر فراخوانی، هرگز Blocking */
EC200U_ModuleState  EC200U_GetModuleState(void);
EC200U_NetworkState EC200U_GetNetworkState(void);

/* --- SIM Card status ---
 * Added specifically so System Management's SIM status LED
 * (system-management.md §3-4) has a real Data source - until now this
 * Contract had no SIM concept at all. Deliberately minimal: only
 * "usable or not", no PIN/PUK-lock state, IMSI, or provider info - add
 * those only when a real consumer needs them, same incremental-capability
 * precedent as the MQTT/Voice-Call-Answer additions above.
 *
 * Named EC200U_SimCard*, not EC200U_Sim*, on purpose - this file already
 * uses "SIM" (lowercase in comments, EC200U_SIM_* in the .c) to mean
 * "Simulator" everywhere else; EC200U_SimCardState avoids that collision.
 *
 * [برداشت/پیشنهاد], not [قطعی] - same open status as the rest of this
 * Contract (OPEN-ITEMS.md 🔴 #1). */
typedef enum {
    EC200U_SIMCARD_NOT_READY,
    EC200U_SIMCARD_READY
} EC200U_SimCardState;

EC200U_SimCardState EC200U_GetSimCardState(void);

/* --- GNSS Capability ---
 * مصرف‌کننده واقعی این توابع فقط GPS Management است. */
typedef struct {
    int32_t  latitude;
    int32_t  longitude;
    uint32_t timestamp;
    bool     fix_valid;
} EC200U_GnssData;

EC200U_Result EC200U_GNSS_Start(void);
EC200U_Result EC200U_GNSS_Stop(void);
EC200U_Result EC200U_GNSS_GetLatest(EC200U_GnssData* data); /* Non-Blocking, Read-Only، منتظر Fix نمی‌ماند */

/* --- SMS Capability ---
 * مصرف‌کننده واقعی این توابع فقط SMS Management است. */
typedef struct {
    const char* destination;
    const char* payload;
    uint32_t    payload_length;
} EC200U_SmsData;

EC200U_Result EC200U_SMS_Send(const EC200U_SmsData* sms);

/* Added when the real AT+CMGS driver replaced the Simulator: a real send
 * (module prompt handshake + SMSC network delivery) cannot complete inside
 * the single Non-Blocking EC200U_SMS_Send() call above the way the old
 * Simulator's instant fake success could. EC200U_SMS_Send() now only means
 * "accepted, an AT sequence just started" (EC200U_OK) or an immediate
 * local rejection; the real outcome is polled here instead:
 *   - EC200U_BUSY  : still in progress - call again later.
 *   - EC200U_OK    : the most recently accepted send was confirmed by the
 *                    module (a final "OK" after the message body).
 *   - EC200U_ERROR : the most recently accepted send failed (module
 *                    "ERROR"/"+CMS ERROR:", or this layer's own Timeout).
 * The OK/ERROR result of one send is only meaningful until the next
 * EC200U_SMS_Send() call starts a new one - same "snapshot of the last
 * attempt" convention as every other polled Capability result in this
 * Contract (e.g. EC200U_MQTT_GetState() after a Connect()). */
EC200U_Result EC200U_SMS_GetSendStatus(void);

EC200U_Result EC200U_SMS_GetReceived(EC200U_SmsData* sms); /* false/EC200U_NOT_READY یعنی چیزی در صف نیست */

/* --- Debug/Runtime simulation (فقط Build میزبان، تا وقتی پیمانکار Component واقعی را تحویل بدهد) ---
 * ماژول فیزیکی هنوز نصب نشده و کد پیمانکار تحویل داده نشده؛ این دو تابع اجازه می‌دهند
 * نتیجه EC200U_SMS_Send() در حین اجرای واقعی (نه فقط Unit Test) به‌صورت دستی کنترل شود -
 * مثلاً از host_main.c یا زیر Debugger صدا زده شوند. حالت Sticky است: هرکدام که آخرین بار
 * صدا زده شود، همان نتیجه را برای همه فراخوانی‌های بعدی EC200U_SMS_Send() برمی‌گرداند تا
 * زمانی که دیگری صدا زده شود - این اجازه می‌دهد رفتار Retry/Fault در SMS Management هم
 * زیر یک شکست پیوسته دیده شود، نه فقط یک شکست تکی.
 * پیش‌فرض (تا وقتی هیچ‌کدام صدا زده نشود): EC200U_NOT_READY - همان رفتار فعلی Stub.
 * این دو تابع بخشی از EC200U Contract نیستند و هیچ Management نباید آن‌ها را صدا بزند؛
 * وقتی Component واقعی پیمانکار جایگزین این فایل شود، این دو تابع (و فایل Stub) کنار
 * می‌روند بدون اینکه SMS Management یا هیچ Consumer دیگری تغییر کند. */
void EC200U_Debug_ForceSmsSendSuccess(void);
void EC200U_Debug_ForceSmsSendFailure(void);
/* حالت Force را کنار می‌گذارد و EC200U_SMS_Send() را به رفتار پیش‌فرض Stub
 * (EC200U_NOT_READY، یعنی "ماژول واقعی هنوز نصب نیست") برمی‌گرداند. */
void EC200U_Debug_ClearSmsSendOverride(void);

/* --- Voice Call Capability ---
 * مصرف‌کننده واقعی این توابع فقط Call Management است.
 * عمداً EC200U_CallState از CallState سطح Management (call_management.h) جداست:
 * این‌جا فقط وضعیت خام مودم است، نه معنای محصولی تماس. */
typedef enum {
    EC200U_CALL_IDLE,
    EC200U_CALL_DIALING,
    EC200U_CALL_RINGING,
    EC200U_CALL_ACTIVE,
    EC200U_CALL_ENDED
} EC200U_CallState;

EC200U_Result    EC200U_Call_Dial(const char* number);
EC200U_Result    EC200U_Call_Hangup(void);
EC200U_CallState EC200U_Call_GetState(void);

/* Explicit Answer step for an incoming call - draft Contract section 4
 * ("Voice Call": Dial/Answer/Hang Up/Call State, all four) already listed
 * this; it was simply never added here yet. Only meaningful while
 * EC200U_Call_GetState() == EC200U_CALL_RINGING; returns EC200U_NOT_READY
 * otherwise (nothing ringing to answer - mirrors EC200U_Call_Hangup()'s own
 * "no call in progress" case, and EC200U_NOT_READY's existing "capability
 * not available right now" meaning elsewhere in this Contract).
 * Deliberately never called by EC200U Manager itself: this Contract does
 * not decide WHO is allowed to answer or WHEN it is safe/authorized to do
 * so - that policy (Caller ID check, listen-in consent, allowlist) belongs
 * entirely to Call Management, above this layer (section 5, "Management
 * محصولی مالک معنای Capability است"). */
EC200U_Result EC200U_Call_Answer(void);

/* Caller ID for a call that is CURRENTLY RINGING IN - draft Contract
 * section 21 lists "Call Incoming" as a scenario this layer must be able to
 * simulate for testing. This is a separate function from
 * EC200U_Call_GetState() on purpose: the calling number is only meaningful
 * (and only available from the modem) for an incoming call that is
 * currently ringing, not as a general, always-readable property of the
 * module or of an outgoing call this side placed itself - so a caller must
 * first observe EC200U_CALL_RINGING via EC200U_Call_GetState(), then ask for
 * the number with this function while that same incoming session is still
 * ringing. buffer/buffer_size follow the same caller-owns-buffer,
 * NUL-terminated pattern as EC200U_Data_Receive() above.
 * Returns:
 *   - EC200U_OK        : buffer now holds the incoming caller's number,
 *                         NUL-terminated (truncated to fit buffer_size if
 *                         necessary).
 *   - EC200U_NOT_READY : no call is currently ringing in - either nothing
 *                         is RINGING at all, or the RINGING session is an
 *                         outgoing call this side dialed itself (no
 *                         incoming number to report either way). buffer is
 *                         left untouched.
 *   - EC200U_ERROR     : buffer is NULL or buffer_size is 0. */
EC200U_Result EC200U_Call_GetIncomingNumber(char* buffer, uint32_t buffer_size);

/* --- Debug/Runtime simulation for Voice Call (host build only, same status
 * as the SMS Debug block above) ---
 * These let a real incoming call and Dial outcome be driven manually while
 * the contractor's real Component does not exist yet:
 *   - EC200U_Debug_SimulateIncomingCall(): moves the simulated call state to
 *     EC200U_CALL_RINGING with the given number available via
 *     EC200U_Call_GetIncomingNumber() - without Call Management (or anyone)
 *     ever calling EC200U_Call_Dial() first, exactly like a real unsolicited
 *     incoming call. number is copied immediately (no ownership kept over
 *     the caller's pointer/buffer afterwards).
 *   - EC200U_Debug_ForceCallDialSuccess/Failure/ClearCallDialOverride():
 *     same Sticky-until-cleared shape as the SMS Send override above,
 *     controls whether EC200U_Call_Dial() accepts a fresh dial attempt or
 *     fails it immediately, for exercising Call Management's own
 *     Timeout/Retry behavior on real hardware.
 * None of these are part of the real EC200U Contract and no Management
 * should call them; they disappear with this Stub file once the
 * contractor's real Component replaces it. */
void EC200U_Debug_SimulateIncomingCall(const char* number);
void EC200U_Debug_ForceCallDialSuccess(void);
void EC200U_Debug_ForceCallDialFailure(void);
void EC200U_Debug_ClearCallDialOverride(void);

/* --- Data / Server transport ---
 * Server Management از این برای انتقال خام داده به Backend استفاده می‌کند؛
 * EC200U Manager نباید بداند این Payload Telemetry است، Event است یا مربوط به Firmware Update. */
EC200U_Result EC200U_Data_Send(const uint8_t* payload, uint32_t length);
EC200U_Result EC200U_Data_Receive(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length);

/* --- MQTT Capability ---
 * Architecture-level Contract capability only - this is NOT a confirmation
 * that MQTT is the final Transport for TCU <-> Backend communication. Per
 * docs/02-management-contracts/backend-contract-tcu-hekar.md section 1,
 * Transport is still [باز] (open): MQTT is the current first candidate,
 * ahead of TCP Socket and HTTP, but the final choice only comes out of the
 * Simulation phase with هکار (same document, section 8). This capability
 * exists so Server Management's own Transport Adapter interface (same
 * document, section 3: "Transport Interface -> MQTT Adapter / TCP Adapter /
 * HTTP Adapter") has something concrete to build against today - exactly
 * the same reasoning the SMS/Voice Call/GNSS Capabilities above were added
 * under before they were confirmed as final product behavior. It is not a
 * claim that TCP/HTTP Adapters will never be needed, or that this exact
 * shape survives Simulation unchanged (OPEN-ITEMS.md "ارتباط TCU ↔ Backend"
 * section, item 1).
 *
 * Per docs/05-outsourcing/ec200u-component-outsourcing-spec.md section 4.6,
 * MQTT (Connect/Disconnect/Publish/Subscribe/Receive/Reconnect/Keep-Alive/
 * Session) is listed alongside Voice Call/SMS/GNSS as a Capability the
 * outsourced EC200U Component must implement - so, exactly like those,
 * Packet Framing/CONNECT/PUBLISH/QoS/Keep-Alive detail stays entirely
 * behind this boundary (project principle #5, "مرز EC200U محکم است").
 * Server Management (or any other Management) must never parse an MQTT
 * packet or assign meaning to a Topic string beyond "the string handed to
 * *_Subscribe() / received back inside an EC200U_MqttMessage" - this layer
 * does not know a Topic implies Event vs Telemetry vs Command; that
 * Envelope-level meaning belongs entirely to Server Management
 * (backend-contract-tcu-hekar.md section 5, "Message Envelope"). */
typedef enum {
    EC200U_MQTT_DISCONNECTED,
    EC200U_MQTT_CONNECTING,
    EC200U_MQTT_CONNECTED
} EC200U_MqttState;

/* topic/payload are caller-owned for the duration of the EC200U_MQTT_Publish()/
 * EC200U_MQTT_Subscribe() call only - neither function keeps a reference to
 * them afterward (same copy-in-do-not-keep-pointer convention
 * EC200U_SMS_Send()/EC200U_Call_Dial() already use for their own const
 * char* arguments). For EC200U_MQTT_GetReceived(), the opposite direction
 * applies: the EC200U_MqttMessage it fills points into storage owned by
 * this layer, valid only until the next EC200U_MQTT_GetReceived() or
 * EC200U_Init() call - same Buffer Ownership shape EC200U_SMS_GetReceived()
 * already documents for EC200U_SmsData above. */
typedef struct {
    const char*    topic;
    const uint8_t* payload;
    uint32_t       payload_length;
} EC200U_MqttMessage;

/* Starts (or re-confirms) a Session against the given Broker. `host`/`port`
 * were added so the caller (Server Management) can actually name a
 * destination - until this Contract grew these two parameters, nothing in
 * the firmware could reach a specific IP/Port at all (see the "IMPORTANT /
 * HONEST TODO" this replaces in server_management_config.h). `host`/`port`
 * come from Configuration Infrastructure (infrastructure/configuration.h's
 * DeviceConfig.communication) - reaching the specific Broker they name over
 * AT Commands/PDP/Socket stays entirely behind this boundary (project
 * principle #5, "مرز EC200U محکم است"); this layer's Stub does not attempt
 * a real connection either way, same as before this parameter was added.
 * Returns:
 *   - EC200U_ERROR     : client_id or host is NULL - no session attempt is
 *                        made.
 *   - EC200U_NOT_READY : module not READY yet.
 *   - EC200U_OK        : accepted - either a fresh CONNECTING attempt just
 *                        started (see EC200U_MQTT_GetState()), or a session
 *                        already CONNECTED/CONNECTING was left alone
 *                        (calling Connect() again while already connected is
 *                        not an error, same idempotent shape as
 *                        EC200U_GNSS_Start() while already active).
 *                        client_id/host are copied immediately, no
 *                        ownership kept over either caller pointer
 *                        afterward. */
EC200U_Result EC200U_MQTT_Connect(const char* client_id, const char* host, uint16_t port);

/* Moves the Session to DISCONNECTED immediately (not staged through
 * EC200U_Process() - symmetrical with EC200U_GNSS_Stop()/
 * EC200U_Call_Hangup() clearing their own state right away, since nothing
 * above this layer needs to observe a transient "disconnecting" state).
 * Calling it while already DISCONNECTED is not an error - same idempotent
 * shape as EC200U_GNSS_Stop(). Always returns EC200U_OK. */
EC200U_Result EC200U_MQTT_Disconnect(void);

EC200U_MqttState EC200U_MQTT_GetState(void);

/* Only meaningful while EC200U_MQTT_GetState() == EC200U_MQTT_CONNECTED -
 * same "capability requires the underlying session to already be up"
 * convention EC200U_Data_Send() applies to EC200U_MODULE_READY. Returns:
 *   - EC200U_ERROR     : message is NULL, or message->topic is NULL, or
 *                        message->payload is NULL while payload_length is
 *                        non-zero (an empty Publish with payload_length 0
 *                        and a NULL payload is valid, same as
 *                        EC200U_Data_Send() accepting length 0).
 *   - EC200U_NOT_READY : Session not CONNECTED yet (DISCONNECTED or
 *                        CONNECTING).
 *   - EC200U_OK        : accepted for transmission - this does not by
 *                        itself guarantee Backend delivery; Server
 *                        Management owns its own Retry/Fault handling above
 *                        this layer, same principle as EC200U_Data_Send(). */
EC200U_Result EC200U_MQTT_Publish(const EC200U_MqttMessage* message);

/* Only meaningful while CONNECTED, same convention as Publish above. topic
 * is copied immediately, no ownership kept over the caller's pointer
 * afterward. Returns EC200U_ERROR if topic is NULL, EC200U_NOT_READY if not
 * CONNECTED, EC200U_OK otherwise (subscription request accepted). */
EC200U_Result EC200U_MQTT_Subscribe(const char* topic);

/* Command از سمت Backend - Pop، نه Peek، دقیقاً هم‌الگوی EC200U_SMS_GetReceived():
 * returns the oldest pending inbound message and removes it from the queue.
 * Returns EC200U_ERROR if message is NULL, EC200U_NOT_READY if nothing is
 * pending (this is the normal "queue empty" case, not a fault - same
 * meaning EC200U_NOT_READY already has for EC200U_SMS_GetReceived()),
 * EC200U_OK otherwise. */
EC200U_Result EC200U_MQTT_GetReceived(EC200U_MqttMessage* message);

/* --- Debug/Runtime simulation for MQTT (host build only, same status as
 * the SMS/Voice Call/Data Debug blocks above) ---
 * The real modem/contractor Component has not arrived yet, so on real
 * hardware (or the host build) EC200U_MQTT_Publish() otherwise always
 * "succeeds" once CONNECTED - these two close that gap for Publish, exactly
 * like EC200U_Debug_ForceDataSendSuccess()/Failure() do for Data_Send.
 * Sticky until cleared: whichever was called last keeps applying to every
 * subsequent EC200U_MQTT_Publish() call, so Server Management's own
 * Retry/Fault behavior can be observed under a sustained failure, not just
 * a single one. Default (until either is called): unchanged Stub/simulator
 * behavior described above (state-based).
 * EC200U_Debug_InjectMqttReceived() simulates a Command arriving from
 * Backend over an already-Subscribed Topic, without needing a real
 * Subscribe/Publish round trip - same purpose as
 * EC200U_Debug_SimulateIncomingCall() for Voice Call. topic/payload are
 * copied immediately (bounded, silently truncated to this layer's internal
 * storage limits, same truncation convention EC200U_Call_GetIncomingNumber()
 * documents); the caller's own pointers/buffers do not need to stay valid
 * afterward. Calling it again before EC200U_MQTT_GetReceived() is called
 * overwrites the previously injected (not yet popped) message - this Stub
 * only models a single-pending-message queue, not a bounded FIFO.
 * None of these four functions are part of the real EC200U Contract and no
 * Management should call them; they disappear with this Stub file once the
 * contractor's real Component replaces it. */
void EC200U_Debug_ForceMqttPublishSuccess(void);
void EC200U_Debug_ForceMqttPublishFailure(void);
void EC200U_Debug_ClearMqttPublishOverride(void);
void EC200U_Debug_InjectMqttReceived(const char* topic, const uint8_t* payload, uint32_t payload_length);

/* --- Debug/Runtime simulation for Data Send (host build only, same status
 * as the SMS Send Debug block above) ---
 * The real modem/contractor Component has not arrived yet, so on real
 * hardware (or the host build) EC200U_Data_Send() otherwise always
 * "succeeds" once the module is READY - there is no way to simulate a send
 * failure to exercise Server Management's own Fault path outside of a
 * mocked EC200U in Ceedling. These two functions close that gap, exactly
 * like EC200U_Debug_ForceSmsSendSuccess()/Failure() do for SMS. Sticky
 * until cleared: whichever was called last keeps applying to every
 * subsequent EC200U_Data_Send() call until the other override or
 * EC200U_Debug_ClearDataSendOverride() is called - this lets Server
 * Management's Retry/Fault behavior be observed under a sustained failure,
 * not just a single one.
 * Default (until either is called): unchanged current Stub behavior -
 * EC200U_OK while the module is READY, EC200U_NOT_READY otherwise.
 * Neither function is part of the real EC200U Contract and no Management
 * should call them; they disappear with this Stub file once the
 * contractor's real Component replaces it. */
void EC200U_Debug_ForceDataSendSuccess(void);
void EC200U_Debug_ForceDataSendFailure(void);
/* حالت Force را کنار می‌گذارد و EC200U_Data_Send() را به رفتار پیش‌فرض Stub
 * (موفق وقتی ماژول READY است، EC200U_NOT_READY وگرنه) برمی‌گرداند. */
void EC200U_Debug_ClearDataSendOverride(void);

/* --- HTTP Client Capability ---
 * Added per docs/02-management-contracts/binary-protocol-final.md section
 * 5/9-2 (OPEN-ITEMS.md "ارتباط TCU <-> Backend" #12, explicit user
 * decision): FOTA's Command `0x00F0` only carries a size + URL, the file
 * itself is fetched over a separate HTTP connection, not the MQTT Command
 * channel/Capability above. This is a NEW Capability, not previously named
 * in docs/03-infrastructure-contracts/ec200u-manager.md or
 * docs/05-outsourcing/ec200u-component-outsourcing-spec.md - both must be
 * updated once this is confirmed with هکار/the EC200U contractor; until
 * then this is this Management-layer Contract's own working assumption,
 * same [برداشت/پیشنهاد] status the MQTT Capability had before it was
 * outsourced-spec'd. Only consumer today: managements/
 * firmware_update_management.c. Same layering/boundary as every other
 * Capability above - no Management may parse an HTTP response header or
 * know the destination is FOTA-related; this layer only moves raw response
 * bytes. */
typedef enum {
    EC200U_HTTP_IDLE,
    EC200U_HTTP_IN_PROGRESS,
    EC200U_HTTP_COMPLETE, /* all response bytes have arrived from the network - GetChunk() may still have buffered bytes left to drain */
    EC200U_HTTP_ERROR
} EC200U_HttpState;

/* Fire-and-forget, same shape as EC200U_MQTT_Connect()/EC200U_GNSS_Start() -
 * starts a GET request for `url` (copied immediately, no ownership kept
 * over the caller's pointer afterward). Returns:
 *   - EC200U_ERROR : url is NULL or empty - no request attempt is made.
 *   - EC200U_BUSY  : a GET is already IN_PROGRESS - Cancel is not modeled
 *                    (no consumer needs it yet); wait for COMPLETE/ERROR
 *                    first, same as EC200U_MQTT_Connect() has no analogous
 *                    case (Connect while already CONNECTED is instead
 *                    treated as idempotent there, but a second, different
 *                    GET while one is in flight is not the same request,
 *                    so BUSY here rather than silently ignoring the new url).
 *   - EC200U_OK    : accepted - a fresh IN_PROGRESS attempt just started
 *                    (poll EC200U_HTTP_GetState()/GetChunk() afterward). */
EC200U_Result EC200U_HTTP_StartGet(const char* url);

EC200U_HttpState EC200U_HTTP_GetState(void);

/* Non-Blocking pop of the next available slice of the response body - same
 * "copy into caller-owned buffer" convention as EC200U_Data_Receive(), not
 * a pointer-into-internal-storage convention like EC200U_MqttMessage
 * (an HTTP body is a plain byte stream, no per-chunk metadata to point at).
 * Returns:
 *   - EC200U_ERROR     : buffer is NULL or buffer_size is 0.
 *   - EC200U_NOT_READY : nothing buffered right now - either still
 *                        IN_PROGRESS with no new bytes yet, or already
 *                        COMPLETE with every byte already drained by a
 *                        previous call. Callers distinguish "still
 *                        fetching" from "finished" via GetState(), not via
 *                        this return value.
 *   - EC200U_OK        : `received_length` (<= buffer_size) bytes were
 *                        copied into `buffer`. May be called again
 *                        immediately for more (this layer never returns
 *                        an END-OF-STREAM marker on its own; GetState() ==
 *                        EC200U_HTTP_COMPLETE plus this returning
 *                        EC200U_NOT_READY together mean "done, nothing
 *                        left"). */
EC200U_Result EC200U_HTTP_GetChunk(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length);

/* --- Debug/Runtime simulation for HTTP (host build only, same status as
 * every other Debug block above) ---
 * The real modem/contractor Component has not arrived yet, so this
 * Simulator has no real network to fetch from - EC200U_Debug_InjectHttpResponse()
 * supplies the bytes EC200U_HTTP_StartGet()'s next attempt "downloads"
 * (copied immediately, bounded/silently truncated to this layer's internal
 * storage limit, same truncation convention EC200U_Debug_InjectMqttReceived()
 * already documents) - without it, every real GET simply stays IN_PROGRESS
 * forever (see ec200u_manager.c). EC200U_Debug_ForceHttpGetFailure()/
 * ClearHttpGetOverride() are the same Sticky-until-cleared pair every other
 * Debug block above uses, for driving Firmware Update Management's own
 * Fault path under a sustained failure. None of these three are part of
 * the real EC200U Contract and no Management should call them; they
 * disappear with this Stub file once the contractor's real Component
 * replaces it. */
void EC200U_Debug_InjectHttpResponse(const uint8_t* data, uint32_t length);
void EC200U_Debug_ForceHttpGetFailure(void);
void EC200U_Debug_ClearHttpGetOverride(void);

#endif /* INFRA_EC200U_MANAGER_H */
