# SmolTxt Architecture

## Overview
SmolTxt is a peer-to-peer encrypted messaging system for children using LoRa radio communication. The system prioritizes simplicity, safety, and parental oversight while providing secure communication within trusted groups called "villages."

---

## Core Concepts

### Villages
- **Definition**: A village is a trusted group of users sharing a single ChaCha20-Poly1305 encryption key (256-bit)
- **Purpose**: Establishes a cryptographic boundary - only members with the key can decrypt messages
- **Namespace**: Village names are human-readable identifiers (e.g., "zMoney", "Chess Club")
- **Key Distribution**: Manual, in-person key sharing via QR code or hex string display
- **Trust Model**: One consent decision by parent/owner grants universal connectivity within village

### Message Types

#### 1. SHOUT (Village-Wide Broadcast)
- **Purpose**: "Hey who's around?" - broadcast to all village members
- **Visibility**: Every member in the village receives it
- **Format**: `SHOUT:villageName:*:senderMAC:message`
- **Use Case**: General announcements, finding who's online

#### 2. GROUP (Filtered Subset)
- **Purpose**: "Need homework help" - messages for a specific topic/group
- **Visibility**: All village members CAN decrypt, but UI filters by group name
- **Format**: `GROUP:villageName:groupName:senderMAC:message`
- **Use Case**: Homework group, sports team coordination, specific topics
- **Note**: Groups are UI filters, not separate encryption boundaries (same village key)

#### 3. WHISPER (Direct Message)
- **Purpose**: "Wanna come over?" - private 1-on-1 communication
- **Visibility**: Only intended recipient sees it in UI
- **Format**: `WHISPER:villageName:recipientMAC:senderMAC:message`
- **Use Case**: Personal conversations between two people
- **Note**: Still encrypted with village key, filtering is client-side

---

## Encryption Architecture

### ChaCha20-Poly1305 Authenticated Encryption
SmolTxt implements military-grade end-to-end encryption using **ChaCha20-Poly1305 AEAD** (Authenticated Encryption with Associated Data), the same cryptographic standard used by Signal, WhatsApp, and TLS 1.3.

#### Encryption Specification
- **Algorithm**: ChaCha20 stream cipher with Poly1305 MAC
- **Standard**: RFC 8439 (ChaCha20-Poly1305 for IETF Protocols)
- **Key Size**: 256 bits (32 bytes) - same strength as AES-256
- **Nonce Size**: 96 bits (12 bytes) - randomly generated per message
- **Authentication Tag**: 128 bits (16 bytes) - Poly1305 MAC for integrity verification
- **Overhead**: 28 bytes per message (12 nonce + 16 tag)

#### Security Properties
1. **Confidentiality**: Messages encrypted with ChaCha20 - unreadable without the key
2. **Authentication**: Poly1305 MAC proves messages come from village member with the key
3. **Integrity**: Any bit flip or modification causes decryption to fail
4. **Protection Against**:
   - Eavesdropping (passive attacks)
   - Message tampering (active attacks)
   - Bit-flipping attacks
   - Message forgery
   - Replay attacks (via message ID deduplication in `seenMessageIds` set)

#### Key Derivation (RFC 8439)
The Poly1305 authentication key is derived from the ChaCha20 keystream per RFC 8439:
```
1. Generate random 12-byte nonce using hardware RNG
2. Initialize ChaCha20 with village key and nonce, counter=0
3. Generate first 32 bytes of keystream → Poly1305 key
4. Reset ChaCha20 counter to 1 for message encryption
5. Encrypt plaintext with ChaCha20
6. Compute Poly1305 MAC over ciphertext using derived key
```

This ensures the authentication key is unique per message and cryptographically bound to the encryption key. No two messages ever use the same Poly1305 key due to random nonces.

#### Encrypted Packet Structure
```
[Nonce (12 bytes)][Ciphertext (variable)][Poly1305 Tag (16 bytes)]
```

**Size Constraints** (LoRa 222-byte payload limit):
- Overhead: 28 bytes (nonce + tag)
- Maximum ciphertext: 194 bytes
- Usable message length: ~134 text characters (after protocol formatting)
- Without authentication: ~150 characters (but vulnerable to tampering)

#### Implementation Details
- **Library**: Crypto 0.4.0 (`ChaCha` and `Poly1305` classes)
- **Encryption Function**: `Encryption::encrypt(plaintext, len, output, outputLen)`
- **Decryption Function**: `Encryption::decrypt(ciphertext, len, output, outputLen)` - returns -1 on MAC failure
- **Constant-Time MAC Comparison**: Prevents timing attacks during tag verification

### Symmetric Encryption (Village Key)
- **Key Generation**: Hardware RNG at village creation
- **Key Storage**: Stored in LittleFS at `/village_[name].txt`
- **Key Format**: 64 hex characters (32 bytes)
- **Key Sharing**: Manual process - owner displays key as hex string or QR code, new member types/scans it in
- **Key Scope**: One key per village - all members share the same key
- **Key Rotation**: New key generated when kicking users (see Access Control section)

### Message Protocol Format
Before encryption, messages are formatted with the protocol:
```
TYPE:villageName:target:senderMAC:messageId:content:currentHop:maxHop
```

**Example Plaintext**:
```
MSG_SHOUT:zMoney:*:f8c43bba2010:1734567890-42:Hello everyone!:0:3
```

**Field Breakdown**:
- `MSG_SHOUT`: Message type (SHOUT/GROUP/WHISPER)
- `zMoney`: Village name (for filtering multi-village radios)
- `*`: Target (wildcard for SHOUT, MAC for WHISPER, group name for GROUP)
- `f8c43bba2010`: Sender MAC address (last 6 bytes)
- `1734567890-42`: Unique message ID (millis + counter)
- `Hello everyone!`: Actual message content
- `0`: Current hop count (for mesh forwarding)
- `3`: Maximum hop limit (3 for SHOUT, 0 for GROUP/WHISPER)

### End-to-End Encryption Flow

#### Sending a Message:
```
1. User types message: "Hello everyone!"
2. Generate unique message ID: millis() + counter
3. Format with protocol: "MSG_SHOUT:zMoney:*:f8c43bba2010:1734567890-42:Hello everyone!:0:3"
4. Generate random 12-byte nonce
5. Derive Poly1305 key from ChaCha20 keystream (counter=0)
6. Encrypt plaintext with ChaCha20 (counter=1) → ciphertext
7. Compute Poly1305 MAC over ciphertext → 16-byte tag
8. Build packet: [nonce][ciphertext][tag]
9. Transmit via LoRa radio (915 MHz, SF9, 125kHz BW, 22 dBm)
```

#### Receiving a Message:
```
1. LoRa interrupt fires (new packet received)
2. Read encrypted packet from radio: [nonce][ciphertext][tag]
3. Extract nonce (12 bytes), ciphertext, tag (16 bytes)
4. Derive Poly1305 key from ChaCha20 keystream using received nonce
5. Compute MAC over received ciphertext
6. Constant-time compare: computed MAC vs received tag
   - If mismatch: Drop silently (authentication failed - wrong key or corrupted)
   - If match: Continue to decryption
7. Decrypt ciphertext with ChaCha20 (counter=1) → plaintext
8. Parse protocol format: "TYPE:village:target:sender:msgId:content:hop:maxHop"
9. Check message ID deduplication (in seenMessageIds set)
   - If seen before: Drop (prevents forwarding loops)
   - If new: Add to seen set, continue
10. Validate village name matches current village
11. Check garbage filter (if unprintable chars >30% → drop silently)
12. Check blocklist (if sender blocked → drop silently)
13. Filter by message type (SHOUT/GROUP/WHISPER) and target
14. Display in UI or forward if needed (based on hop count)
```

### Garbage Detection
Messages that fail decryption (wrong key, tampered data, transmission errors) produce random binary garbage. The Poly1305 MAC catches most of these, but a secondary garbage filter provides defense-in-depth:

```cpp
bool isGarbage(String text) {
    int unprintableCount = 0;
    for (char c : text) {
        if (c < 32 && c != '\n' && c != '\t') {
            unprintableCount++;
        }
    }
    return (unprintableCount > text.length() * 0.3); // >30% unprintable = drop
}
```

**Purpose**: 
- Extra protection against MAC bypass attacks
- Hides failed decryptions from kicked users (they see silence, not gibberish)
- Handles rare transmission errors gracefully
- Clean UI with no cryptographic noise

**Note**: With Poly1305 authentication, garbage detection is mostly redundant but kept for defense-in-depth.

---

## Access Control

### Kicking (Village-Level)
**Problem**: Someone needs to be removed from village entirely

**Solution**: Key Rotation Protocol
```
1. Owner selects "Kick User: Dave"
2. System generates new ChaCha20-Poly1305 key (256-bit)
3. Broadcast message (encrypted with OLD key):
   "KICK:44c2f85ee937:NEW_KEY:B7D4C992..."
4. All devices process:
   - If my MAC == 44c2f85ee937 (Dave): Delete village, show "You were removed"
   - Else: Update to new key, continue messaging
```

**Result**:
- Dave's device (if honest) deletes village
- Dave's device (if modified) ignores command BUT old key is useless
- New messages encrypted with new key Dave doesn't have
- Dave's decryption attempts fail MAC verification → dropped silently
- Dave sees silence (no garbage, no errors)
- Seamless for remaining members

**User Experience**:
- Owner: One tap, one confirmation → done
- Kicked user: Messages stop appearing (can't decrypt with old key)
- Remaining members: Automatic key update, no interruption

**Note**: Key rotation not yet implemented in current version - users must manually create new village and re-share key to kick someone.

### Blocking (User-Level)
**Problem**: Emma is being harassed by Dave, but doesn't want to leave village or involve owner

**Solution**: Client-Side Global Blocklist

#### Storage
- **File**: `/blocklist.txt` in LittleFS
- **Format**: One MAC address per line (uint64_t in hex)
```
f8c43bba2010
22b1e94dd726
```

#### Behavior
```cpp
// In message processing:
String msg = decrypt(packet, villageKey);
uint64_t senderMAC = extractMAC(msg);

if (blockList.isBlocked(senderMAC)) {
    return ""; // Silently drop - no display, no notification
}
```

**Effect Across All Message Types**:
- SHOUT from Dave → dropped
- GROUP messages from Dave → dropped  
- WHISPER from Dave → dropped
- Works in ALL villages Emma is in

**Properties**:
- **Local only**: Only Emma's device filters Dave
- **Invisible**: Dave never knows he's blocked
- **Non-cryptographic**: Emma still receives/decrypts Dave's messages, just doesn't display them
- **Reversible**: Emma can unblock anytime from Settings menu

**Limitations**:
- Dave could spoof MAC address if he modifies firmware (requires programming skills)
- Acceptable risk for target audience (young children unlikely to have skills)
- Can be upgraded to Ed25519 signatures later if MAC spoofing becomes real issue

#### UI Flow
```
View message from Dave
├── Long-press or menu button
├── "Block User: Dave (f8c4...2010)"
└── [Confirm] → Added to global blocklist

Settings Menu
└── Blocked Users
    ├── Dave (f8c43bba2010) [Unblock]
    └── Frank (22b1e94dd726) [Unblock]
```

---

## MAC Address Spoofing (Future Consideration)

### Current Approach (MVP)
- **Trust-based**: Sender MAC included in message plaintext
- **Vulnerability**: Technically possible to fake MAC by modifying firmware
- **Mitigation**: Target audience (children) unlikely to have skills
- **Benefit**: Simpler code, ~150 character messages

### Future Enhancement: Ed25519 Signatures
If MAC spoofing becomes a real problem, can upgrade to signed messages:

**Message Format with Signature**:
```
"SHOUT:zMoney:*:f8c43bba2010:Hello!" + 64-byte Ed25519 signature
```

**Key Management**:
- Each device generates Ed25519 keypair at first boot
- Private key stored in flash (never shared)
- Public key derived from private key
- Public keys exchanged when joining village

**Verification**:
```cpp
bool verified = crypto.verify(message, signature, senderPublicKey);
if (!verified) {
    return ""; // Spoofed message, drop it
}
```

**Trade-offs**:
- **Pro**: Cryptographically enforced identity, can't fake MAC without stealing device
- **Pro**: Blocking becomes unbypassable
- **Con**: ~50 characters less per message (~100 chars vs ~150 chars)
- **Con**: More complex key management
- **Con**: Need to track public keys for all village members

**Decision**: Defer to v2 unless MAC spoofing attacks observed in practice

---

## Message Size Constraints

### LoRa Packet Limits
- **Maximum payload**: 222 bytes (SF9, 125kHz BW, 915 MHz)

### Current Implementation (ChaCha20-Poly1305)
```
Nonce: = 12 bytes
Poly1305 tag: = 16 bytes
Protocol overhead: "MSG_SHOUT:zMoney:*:f8c43bba2010:1734567890-42::0:3" = ~50 bytes
Total overhead: 12 + 16 + 50 = ~78 bytes
Available for text: 222 - 78 = ~144 bytes
Practical limit: ~134 characters
```

**Current Message Capacity**: ~134 text characters with full authenticated encryption

### Alternative: Without Authentication (ChaCha20 Only)
```
Nonce: = 12 bytes
Protocol overhead: ~50 bytes
Total overhead: ~62 bytes
Available for text: 222 - 62 = ~160 bytes
Practical limit: ~150 characters
```

**Trade-off**: +16 characters, but vulnerable to message tampering and forgery

### Future: With Ed25519 Signatures (If Needed)
```
Nonce: = 12 bytes
Poly1305 tag: = 16 bytes
Ed25519 signature: = 64 bytes
Protocol overhead: ~50 bytes
Total overhead: 12 + 16 + 64 + 50 = ~142 bytes
Available for text: 222 - 142 = ~80 bytes
Practical limit: ~70-75 characters
```

**Not currently implemented** - Poly1305 MAC provides sufficient authentication for shared-key scenario

### Character Limit Recommendation
**Without signatures**: ~150 characters is plenty for typical kids' messages
- "Hey who wants to play basketball at recess?" (46 chars)
- "Can someone help me with the math homework problem #5?" (56 chars)

---

## Village Membership & Roster Management

### No Central Roster (By Design)
- **No server**: Pure peer-to-peer, no central authority
- **No member list sync**: Each device only knows about itself
- **Discovery**: Devices learn about other members by seeing their messages

### Owner Privileges
The village creator/owner has special abilities:
- **Kick members**: Rotate encryption key to exclude someone
- **Share key**: Display village key for new members to join

### Joining a Village
```
New member (Bob) wants to join "zMoney":
1. Meets owner (Alice) in person
2. Alice: Village Settings → "Show Key"
3. Alice's screen displays QR code + hex key
4. Bob: Main Menu → "Join Village by Key"
5. Bob enters village name "zMoney" and hex key
6. Bob's device saves key to /village_zMoney.txt
7. Bob can now decrypt messages, send messages
8. Other members discover Bob when they see his first message
```

**No pre-approval needed**: Anyone with the key can join (trust via key possession)

### Member Discovery
Members learn about each other organically:
```
Alice creates village, sends SHOUT
Bob joins, sends SHOUT → Alice sees "Bob is here"
Carol joins, sends SHOUT → Alice and Bob see "Carol is here"
```

**No formal roster**: Just "people I've seen messages from"

---

## Security Considerations

### Threat Model

#### What We Protect Against:
✅ **Eavesdropping**: ChaCha20-Poly1305 encryption prevents unauthorized decryption  
✅ **Unauthorized joining**: Must have 256-bit key (cannot brute force)  
✅ **Harassment**: Blocking filters unwanted messages locally  
✅ **Village infiltration**: Key must be shared in-person (hard to intercept)

#### What We DON'T Protect Against (Acceptable Risks):
❌ **Key compromise**: If key is shared with malicious actor, they're in (social problem, not technical)  
❌ **Device theft**: Physical access = game over (could extract keys from flash)  
❌ **Firmware modification**: Advanced users could bypass blocks/limits (requires programming skills)  
❌ **MAC spoofing**: Without signatures, sender identity is trust-based (deferred to v2)  
❌ **Replay attacks**: Old messages could be re-transmitted (low impact for casual messaging)

### Parent/Owner Responsibilities
The system relies on responsible key management:
- Only share village keys with trusted individuals in-person
- Review who has been given keys
- Rotate key (kick all, re-invite) if key is compromised
- Teach children not to share keys via insecure channels

### Privacy Properties
- **No metadata leakage**: No server, no logs, no tracking
- **No location tracking**: LoRa is peer-to-peer radio (no GPS required)
- **No internet required**: Fully offline operation
- **Ephemeral by default**: Messages only stored locally, not backed up
- **No phone numbers**: Identity is just a MAC address + chosen username

---

## Mesh Networking: Smart Hybrid Approach

### Problem: LoRa Range Limitations
Direct LoRa radio range is typically 1-3 km in urban environments. Users may be spread across larger areas (school campus, neighborhood) requiring message relay.

### Solution: Direct-First with Mesh Escalation

Instead of always using mesh forwarding (battery intensive) or only direct broadcast (limited range), we implement **intelligent escalation**:

#### **SHOUT Messages (Broadcast to All)**
```
Alice sends: "Hey everyone!"
├─ Immediately broadcast with max_hop=3 (allow forwarding)
├─ All devices within 3 hops receive and forward
└─ Maximum coverage from the start
```

**Rationale**: SHOUT is meant for everyone, so use mesh immediately for widest reach.

#### **WHISPER Messages (Direct 1-on-1)**
```
Alice sends: "Wanna hang out?" to Dave

Attempt 1: Direct delivery (0:00 - 2:00 seconds)
├─ Send with max_hop=0 (no forwarding allowed)
├─ Wait 2 seconds for ACK from Dave
├─ Dave's ACK received? → SUCCESS, done in <2s ✓
└─ No ACK? → Escalate to Attempt 2

Attempt 2: Mesh delivery (2:00 - 7:00 seconds)
├─ Resend with max_hop=3 (allow 3 hops)
├─ Intermediate devices forward the message
├─ Wait 5 seconds for ACK
├─ Dave's ACK received? → SUCCESS, done in <7s ✓
└─ No ACK? → FAILED, notify sender "Dave not reachable"
```

**Rationale**: Most direct messages are to nearby friends. Try fast direct delivery first, only use mesh if needed.

#### **GROUP Messages (Selective Escalation)**
```
Alice sends: "Need homework help" to Homework group (Bob, Carol, Dave)

Attempt 1: Direct broadcast
├─ Send with max_hop=0
├─ Wait 2 seconds
├─ Bob ACK ✓, Carol ACK ✓, Dave no response
└─ Bob and Carol delivered successfully

Attempt 2: Mesh retry for missing recipients
├─ Resend ONLY for Dave with max_hop=3
├─ Wait 5 seconds
└─ Dave ACK ✓ or timeout → update UI accordingly
```

**Rationale**: Selective escalation - only use mesh for recipients who didn't respond to direct attempt.

### Message Format

All messages include hop control fields:

```
"SHOUT:zMoney:*:alice_mac:msg_12345:Hello!:hop_0:max_3"
                                           ↑     ↑
                                    current hop  max hops allowed
```

**Fields**:
- `hop_X`: Current hop count (incremented by each forwarder)
- `max_Y`: Maximum hops allowed (set by sender based on message type/attempt)

### Forwarding Logic

Every device implements this forwarding rule:

```cpp
void onReceiveMessage(String msg) {
    int currentHop = extractCurrentHop(msg);
    int maxHop = extractMaxHop(msg);
    String msgId = extractMessageId(msg);
    
    // Check 1: Hop limit reached?
    if (currentHop >= maxHop) {
        return; // Don't forward
    }
    
    // Check 2: Already seen this message?
    if (seenMessages.count(msgId)) {
        return; // Prevent duplicate forwarding
    }
    seenMessages.insert(msgId);
    
    // Check 3: Am I the intended recipient?
    if (isForMe(msg)) {
        processMessage(msg);
        sendACK(msg); // Acknowledge receipt
        // Don't forward messages addressed to me
        return;
    }
    
    // Forward the message
    delay(random(50, 200)); // Random delay to avoid collisions
    msg = incrementHop(msg); // hop_0 → hop_1
    radio.transmit(msg);
}
```

### Acknowledgment (ACK) System

Recipients automatically send tiny ACK messages back to sender:

```
Message: "WHISPER:zMoney:dave:alice:msg_123:Hello!"
ACK:     "ACK:alice:dave:msg_123"
         ↑     ↑     ↑    ↑
         type  to    from msgId
```

**ACK forwarding**: ACKs are also subject to mesh forwarding if sender is out of direct range.

**Sender logic**:
```cpp
struct PendingMessage {
    String content;
    String recipientMAC;
    unsigned long sentTime;
    int attempt; // 0=direct, 1=mesh
    std::set<String> receivedACKs;
};

void loop() {
    for (auto &pm : pendingMessages) {
        unsigned long elapsed = millis() - pm.sentTime;
        
        if (pm.attempt == 0 && elapsed > 2000) {
            // Direct attempt timeout, escalate to mesh
            sendWithHopLimit(pm.content, 3);
            pm.attempt = 1;
            pm.sentTime = millis();
        }
        
        if (pm.attempt == 1 && elapsed > 5000) {
            // Mesh attempt timeout, give up
            notifyUI("Message to " + pm.recipientMAC + " failed");
            removePending(pm);
        }
    }
}
```

### Duplicate Detection

Each device maintains a rolling window of recently seen message IDs:

```cpp
std::set<String> seenMessages; // Last 100 message IDs
unsigned long lastCleanup = 0;

void cleanupSeenMessages() {
    if (millis() - lastCleanup > 60000) { // Every 60 seconds
        seenMessages.clear(); // Clear old IDs
        lastCleanup = millis();
    }
}
```

**Purpose**: Prevents infinite forwarding loops in mesh network.

### Collision Avoidance

When multiple devices receive the same message and forward simultaneously, radio collisions occur. Mitigation:

```cpp
// Random delay before forwarding (50-200ms)
delay(random(50, 200));
```

**Why it works**: Spreads transmissions over time, reduces probability of collision.

### Performance Characteristics

| Scenario | Direct (80%) | Mesh (20%) | Average |
|----------|--------------|------------|---------|
| **Latency** | 200-400ms | 2-7 seconds | ~1.5s |
| **Battery per msg** | 1x TX | 1x TX + retries | ~1.2x TX |
| **Success rate** | 80% | 15% | 95% total |

**Assumptions**:
- 80% of messages succeed with direct transmission (users typically nearby)
- 20% require mesh escalation (users far apart)
- 15% of those succeed with mesh (5% permanently out of range/offline)

### Range Extension

**Direct broadcast**: ~1-3 km radius  
**1-hop mesh**: ~2-6 km radius  
**2-hop mesh**: ~3-9 km radius  
**3-hop mesh**: ~4-12 km radius (covers most school campuses)

**Trade-off**: Each hop adds ~200ms latency and increases radio congestion.

### UI Feedback

Users see delivery status in real-time:

```
Alice's screen after sending WHISPER to Dave:

[10:23] Me: Wanna hang out?
        📡 Sending... (0-0.2s)
        ⏱️ Waiting for Dave... (0.2-2s direct attempt)
        🔄 Retrying via mesh... (2-7s if direct failed)
        ✓✓ Delivered to Dave (if successful)
        ⚠️ Dave not reachable (if both attempts failed)
```

### Battery Optimization

**Without smart escalation** (always use mesh):
- Every message forwarded by all devices
- 10 users × 10 messages/hour = 100 transmissions/hour per device
- High battery drain

**With smart escalation**:
- Most messages (80%) delivered direct, no forwarding
- Only 20% trigger mesh forwarding
- 10 users × 10 messages/hour × 0.2 mesh rate = 20 transmissions/hour per device
- **5x less battery usage** compared to always-mesh

### Configuration

Future enhancement: Allow users to control mesh behavior:

```
Settings → Mesh Networking
├─ Enable Mesh Forwarding: [ON] / OFF
│  └─ "Help relay messages (uses more battery)"
├─ Max Hops: 1 / 2 / [3] / 4 / 5
│  └─ "Higher = longer range, more battery"
└─ Direct Timeout: 1s / [2s] / 3s / 5s
   └─ "Time to wait before using mesh"
```

**Default**: Mesh ON, 3 hops max, 2s direct timeout (balances range and battery).

---

## Future Enhancements

### Deferred Features (Not in MVP)
1. **Beacon/Discovery Mode**: Allow villages to broadcast existence (privacy/safety concerns)
2. **Ed25519 Signatures**: Cryptographic sender identity (if MAC spoofing becomes issue)
3. **Multi-key Groups**: Separate encryption per group within village (more complexity)
4. **Message History Persistence**: Save chat history to flash (currently RAM-only)
5. **Typing Indicators**: Show when someone is composing (more radio traffic)
6. **File Sharing**: Images, audio clips (LoRa bandwidth too limited)
7. **No Network Warning**: Display "No network" on napping screen when disconnected (implemented in v0.42.5)

### Upgrade Path: Ed25519 Signatures
If MAC spoofing attacks are observed:
1. Add `Crypto` library dependency
2. Generate Ed25519 keypair at first boot → store in `/device_key.bin`
3. Modify message format: append 64-byte signature
4. Exchange public keys during village join (add to village file)
5. Verify signature on all received messages
6. Character limit drops from ~150 to ~100 (acceptable trade-off)

---

## Implementation Status

### Completed
- ✅ Village encryption key generation and storage
- ✅ ChaCha20-Poly1305 authenticated encryption/decryption
- ✅ LoRa radio communication (interrupt-driven async)
- ✅ Message format parsing (SHOUT/GROUP/WHISPER)
- ✅ Partial refresh display (776ms, smooth UX)
- ✅ Keyboard input (CardKB I2C)

### In Progress
- ⏳ Message composition UI
- ⏳ Chat history display
- ⏳ Village key sharing UI (QR code + hex display)
- ⏳ Join village by key flow
- ⏳ BlockList class implementation
- ⏳ Kick user + key rotation

### Not Started
- ❌ Group creation/management UI
- ❌ Whisper (direct message) flow
- ❌ Settings menu (blocked users, etc.)
- ❌ Message history persistence
- ❌ Username management

---

## Design Rationale

### Why One Key Per Village?
**Simplicity for target audience**: Kids and parents can understand "one secret per group"
- Easy to explain: "Only people with the secret word can join"
- Matches real-world social dynamics: "Our friend group has a secret handshake"
- Parental oversight: Parent approves ONE village membership decision

**Alternative (multi-key groups)**: Would require managing multiple keys per device, complex key rotation, harder to explain

### Why Client-Side Blocking Instead of Cryptographic Exclusion?
**Practical effectiveness vs. complexity**:
- Most kids won't have firmware modification skills
- Simpler implementation (no key rotation per block)
- Doesn't require village owner involvement (Emma can protect herself)
- Reversible without coordination

**Cryptographic blocking (separate group keys)**: Would solve spoofing but adds significant complexity

### Why No Central Server?
**Privacy and simplicity**:
- No account registration, no phone numbers, no email
- No dependency on internet connectivity
- No metadata logging or surveillance
- Works offline, in rural areas, during emergencies
- True peer-to-peer communication

### Why Manual Key Sharing?
**Security via intentionality**:
- Forces in-person interaction (parents meet other kids' parents)
- Prevents accidental/malicious key spreading
- Key ceremony builds social trust
- QR code makes it easy but still requires physical proximity

---

## Tech Stack Summary

- **Hardware**: Heltec Vision Master E290 (ESP32-S3, SX1262 LoRa, e-paper display)
- **Radio**: 915 MHz LoRa, SF9, 125 kHz BW, 22 dBm power
- **Display**: GxEPD2 with partial refresh (~776ms)
- **Encryption**: ChaCha20-Poly1305 AEAD (Arduino Crypto 0.4.0 library)
- **Storage**: LittleFS for village keys, blocklist, settings
- **Input**: M5Stack CardKB (I2C keyboard)
- **Framework**: Arduino/PlatformIO on ESP32-S3

---

## File Structure

```
/village_[name].txt    - Village encryption key (32 bytes hex)
/blocklist.txt         - Blocked MAC addresses (one per line)
/settings.txt          - Device settings (username, etc.)
```

---

*Last Updated: 2025-12-03*
