#pragma once
#include <cstdint>
#include <vector>

namespace android::hardware::nfc {
namespace V1_1 {
enum class NfcEvent : uint32_t {
    OPEN_CPLT = 0, CLOSE_CPLT = 1, POST_INIT_CPLT = 2, PRE_DISCOVER_CPLT = 3,
    REQUEST_CONTROL = 4, RELEASE_CONTROL = 5, ERROR = 6, HCI_NETWORK_RESET = 7,
};
enum class PresenceCheckAlgorithm : uint8_t {
    DEFAULT = 0, I_BLOCK = 1, ISO_DEP_NAK = 2,
};
struct ProtocolDiscoveryConfig {
    uint8_t protocol18092Active = 0;
    uint8_t protocolBPrime = 0;
    uint8_t protocolDual = 0;
    uint8_t protocol15693 = 0;
    uint8_t protocolKovio = 0;
    uint8_t protocolMifare = 0;
    uint8_t discoveryPollKovio = 0;
    uint8_t discoveryPollBPrime = 0;
    uint8_t discoveryListenBPrime = 0;
};
struct NfcConfig {
    bool nfaPollBailOutMode = false;
    PresenceCheckAlgorithm presenceCheckAlgorithm = PresenceCheckAlgorithm::DEFAULT;
    uint8_t defaultOffHostRoute = 0;
    uint8_t defaultOffHostRouteFelica = 0;
    uint8_t defaultSystemCodeRoute = 0;
    uint8_t defaultSystemCodePowerState = 0;
    uint8_t defaultRoute = 0;
    uint8_t offHostESEPipeId = 0;
    uint8_t offHostSIMPipeId = 0;
    uint32_t maxIsoDepTransceiveLength = 0;
    std::vector<uint8_t> hostWhitelist;
    ProtocolDiscoveryConfig nfaProprietaryCfg{};
};
}  // namespace V1_1
namespace V1_2 {
struct NfcConfig {
    V1_1::NfcConfig v1_1;
    std::vector<uint8_t> offHostRouteUicc;
    std::vector<uint8_t> offHostRouteEse;
    uint8_t defaultIsoDepRoute = 0;
};
}  // namespace V1_2
}  // namespace android::hardware::nfc
