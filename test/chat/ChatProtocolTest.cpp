#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "AppCapabilities.h"
#include "AppVisibility.h"
#include "activities/apps/chat/RadioChatProtocol.h"

using namespace chat;

TEST(ChatProtocol, MaximumUtf8FrameFitsRadioAndRoundTripsUnaligned) {
  radio::Packet tx;
  tx.type = radio::Type::Text;
  tx.sender = 0x0123456789abcdefULL;
  tx.recipient = 0xfedcba9876543210ULL;
  tx.session = 0x87654321;
  tx.sequence = 42;
  for (size_t i = 0; i < MAX_TEXT; i += 3) std::memcpy(tx.text + i, "纸", 3);
  std::array<uint8_t, 256> buffer{};
  const auto length = radio::encode(tx, buffer.data() + 1, buffer.size() - 1);
  EXPECT_EQ(length, radio::MAX_FRAME);
  EXPECT_LE(length, 255u);
  radio::Packet rx;
  ASSERT_TRUE(radio::decode(buffer.data() + 1, length, rx));
  EXPECT_EQ(rx.sender, tx.sender);
  EXPECT_EQ(rx.recipient, tx.recipient);
  EXPECT_EQ(rx.session, tx.session);
  EXPECT_EQ(rx.sequence, tx.sequence);
  EXPECT_STREQ(rx.text, tx.text);
  EXPECT_EQ(radio::encode(tx, buffer.data(), length - 1), 0u);
}

TEST(ChatProtocol, RejectsMalformedUtf8AndControlCharacters) {
  const char* invalid[] = {"", "a\nb", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8", "\x80", "\x7f"};
  for (auto text : invalid) EXPECT_FALSE(validText(reinterpret_cast<const uint8_t*>(text), std::strlen(text)));
  const uint8_t nul[] = {'a', 0, 'b'};
  EXPECT_FALSE(validText(nul, sizeof(nul)));
  std::array<uint8_t, MAX_TEXT + 1> oversized;
  oversized.fill('a');
  EXPECT_FALSE(validText(oversized.data(), oversized.size()));
  EXPECT_TRUE(validText(reinterpret_cast<const uint8_t*>("你好🙂"), 10));
}

TEST(ChatProtocol, RejectsTruncationUnknownVersionsAndNonTextPayloads) {
  radio::Packet tx;
  tx.sender = 123;
  tx.sequence = 456;
  std::array<uint8_t, 256> buffer{};
  const auto length = radio::encode(tx, buffer.data(), buffer.size());
  radio::Packet rx;
  for (size_t size = 0; size < length; ++size) EXPECT_FALSE(radio::decode(buffer.data(), size, rx));
  buffer[2] = 2;
  EXPECT_FALSE(radio::decode(buffer.data(), length, rx));
  buffer[2] = 1;
  buffer[3] = 255;
  EXPECT_FALSE(radio::decode(buffer.data(), length, rx));
  buffer[3] = static_cast<uint8_t>(radio::Type::Ack);
  EXPECT_FALSE(radio::decode(buffer.data(), length + 1, rx));
  buffer[3] = static_cast<uint8_t>(radio::Type::Text);
  EXPECT_FALSE(radio::decode(buffer.data(), length, rx));
  std::memset(buffer.data() + 4, 0, 8);
  EXPECT_FALSE(radio::decode(buffer.data(), length, rx));
}

TEST(ChatProtocol, DuplicateWindowDistinguishesSenderAndBootSession) {
  radio::DuplicateWindow window;
  radio::Packet packet;
  packet.sender = 1;
  packet.session = 2;
  packet.sequence = 3;
  EXPECT_FALSE(window.contains(packet));
  window.remember(packet);
  EXPECT_TRUE(window.contains(packet));
  ++packet.sender;
  EXPECT_FALSE(window.contains(packet));
  --packet.sender;
  ++packet.session;
  EXPECT_FALSE(window.contains(packet));
  --packet.session;
  for (uint32_t sequence = 4; sequence < 36; ++sequence) {
    packet.sequence = sequence;
    window.remember(packet);
  }
  packet.sequence = 3;
  EXPECT_FALSE(window.contains(packet));
}

TEST(ChatCapabilities, HardwareRequirementsDoNotAffectOrdinaryApps) {
  using namespace appCapabilities;
  constexpr Device reader{false, false}, paperS3{false, true, true}, paperMono{true, true, true};
  EXPECT_TRUE(allows(Requirement::None, reader));
  EXPECT_FALSE(allows(Requirement::Radio, paperS3));
  EXPECT_FALSE(allows(Requirement::WifiChat, reader));
  EXPECT_TRUE(allows(Requirement::WifiChat, paperS3));
  EXPECT_TRUE(allows(Requirement::Radio, paperMono));
  EXPECT_TRUE(allows(Requirement::EspNow, paperMono));
  EXPECT_TRUE(allows(Requirement::EspNow, paperS3));
  EXPECT_FALSE(allows(Requirement::EspNow, reader));
  EXPECT_EQ(static_cast<unsigned>(appVisibility::AppId::Library), 17u);
  EXPECT_EQ(static_cast<unsigned>(appVisibility::AppId::RfChat), 18u);
  EXPECT_EQ(static_cast<unsigned>(appVisibility::AppId::WifiChat), 19u);
  EXPECT_EQ(static_cast<unsigned>(appVisibility::AppId::EspChat), 20u);
}

#include "activities/apps/chat/RadioPresence.h"
#include "system/MessageInbox.h"

TEST(ChatProtocol, ExitBeaconRoundTripsAndRejectsPayload) {
  radio::Packet tx;
  tx.type = radio::Type::Bye;
  tx.sender = 7;
  tx.sequence = 9;
  uint8_t bytes[radio::MAX_FRAME]{};
  const auto size = radio::encode(tx, bytes, sizeof(bytes));
  ASSERT_EQ(size, radio::HEADER);
  radio::Packet rx;
  ASSERT_TRUE(radio::decode(bytes, size, rx));
  EXPECT_EQ(rx.type, radio::Type::Bye);
  tx.text[0] = 'x';
  EXPECT_EQ(radio::encode(tx, bytes, sizeof(bytes)), 0u);
  EXPECT_FALSE(radio::decode(bytes, size + 1, rx));
}
TEST(ChatPresence, ExpiryExitAndReturnWorkAcrossClockWrap) {
  radio::Presence peers;
  const uint32_t start = UINT32_MAX - 1000;
  peers.seen(7, start);
  EXPECT_TRUE(peers.online(7, start + radio::Presence::TIMEOUT_MS - 1));
  EXPECT_FALSE(peers.online(7, start + radio::Presence::TIMEOUT_MS));
  peers.seen(7, 50000);
  EXPECT_TRUE(peers.online(7, 50000));
  peers.leave(7);
  EXPECT_FALSE(peers.online(7, 50001));
  peers.seen(7, 50002);
  EXPECT_TRUE(peers.online(7, 50003));
  EXPECT_FALSE(peers.online(8, 50003));
}
TEST(ChatPresence, CapacityDoesNotEvictKnownPeersOrAcceptZero) {
  radio::Presence peers;
  peers.seen(0, 10);
  EXPECT_FALSE(peers.online(0, 10));
  for (size_t i = 1; i <= MAX_CONTACTS; ++i) peers.seen(i, 10);
  peers.seen(99, 10);
  EXPECT_FALSE(peers.online(99, 10));
  EXPECT_TRUE(peers.online(1, 10));
  peers.seen(1, 40000);
  EXPECT_TRUE(peers.online(1, 60000));
  EXPECT_FALSE(peers.online(2, 60000));
}
TEST(SystemInbox, CoalescesByApplicationAndConversationAndClearsOnlyOpenedPeer) {
  using namespace systemMessages;
  Inbox inbox;
  inbox.post(Source::Radio, "peer1", "first");
  inbox.post(Source::Radio, "peer1", "second");
  inbox.post(Source::Wifi, "peer1", "wifi");
  inbox.post(Source::Radio, "peer2", "other");
  ASSERT_EQ(inbox.count, 3);
  EXPECT_EQ(inbox.unread(), 4u);
  EXPECT_STREQ(inbox.items[0].preview, "second");
  EXPECT_EQ(inbox.items[0].unread, 2);
  inbox.read(Source::Radio, "peer1");
  ASSERT_EQ(inbox.count, 2);
  EXPECT_EQ(inbox.unread(), 2u);
  EXPECT_EQ(inbox.items[0].source, Source::Wifi);
  EXPECT_STREQ(inbox.items[1].peer, "peer2");
  const auto revision = inbox.revision;
  inbox.read(Source::Radio, "missing");
  EXPECT_EQ(inbox.revision, revision);
}
TEST(SystemInbox, CapacityAndCounterAreBoundedAndOversizedInputIsRejected) {
  using namespace systemMessages;
  Inbox inbox;
  for (unsigned i = 0; i < Inbox::CAPACITY + 2; ++i) {
    char peer[8];
    std::snprintf(peer, sizeof(peer), "%u", i);
    inbox.post(Source::Radio, peer, "text");
  }
  ASSERT_EQ(inbox.count, Inbox::CAPACITY);
  EXPECT_STREQ(inbox.items[0].peer, "2");
  for (unsigned i = 0; i <= UINT16_MAX; ++i) inbox.post(Source::Radio, "2", "latest");
  EXPECT_EQ(inbox.items[0].unread, UINT16_MAX);
  std::string tooLong(193, 'a');
  const auto revision = inbox.revision;
  inbox.post(Source::Wifi, "bad", tooLong.c_str());
  EXPECT_EQ(inbox.revision, revision);
}
TEST(SystemInbox, FriendRequestRemainsDistinctFromMessagePreview) {
  using namespace systemMessages;
  Inbox inbox;
  inbox.post(Source::Wifi, "new-friend", "", NoticeKind::FriendRequest);
  ASSERT_EQ(inbox.count, 1);
  EXPECT_EQ(inbox.items[0].kind, NoticeKind::FriendRequest);
  EXPECT_EQ(inbox.unread(), 1u);
  inbox.post(Source::Radio, "new-friend", "radio text");
  inbox.read(Source::Wifi, "new-friend");
  ASSERT_EQ(inbox.count, 1);
  EXPECT_EQ(inbox.items[0].source, Source::Radio);
  inbox.post(Source::Wifi, "new-friend", "after accepting");
  ASSERT_EQ(inbox.count, 2);
  EXPECT_EQ(inbox.items[1].kind, NoticeKind::Message);
  EXPECT_STREQ(inbox.items[1].preview, "after accepting");
}

namespace {
nearby::Packet nearbyPacket(uint8_t key) {
  nearby::Packet packet;
  packet.address[0] = key;
  packet.address[32] = 0x12;
  packet.address[35] = 0x34;
  packet.address[36] = key ^ 0x12;
  packet.address[37] = 0x34;
  packet.dht[0] = key;
  packet.port = 33445;
  return packet;
}
}  // namespace

TEST(WifiNearby, RoundTripHasExactSizeVersionAndNetworkByteOrder) {
  const auto packet = nearbyPacket(7);
  uint8_t wire[nearby::FRAME_BYTES + 1]{};
  ASSERT_TRUE(nearby::encode(packet, wire + 1, nearby::FRAME_BYTES));
  EXPECT_EQ(wire[77], 0x82);
  EXPECT_EQ(wire[78], 0xa5);
  nearby::Packet received;
  ASSERT_TRUE(nearby::decode(wire + 1, nearby::FRAME_BYTES, received));
  EXPECT_EQ(received.port, packet.port);
  EXPECT_EQ(std::memcmp(received.address, packet.address, nearby::ADDRESS_BYTES), 0);
  EXPECT_EQ(std::memcmp(received.dht, packet.dht, nearby::KEY_BYTES), 0);
  EXPECT_FALSE(nearby::encode(packet, wire, nearby::FRAME_BYTES - 1));
  for (size_t size = 0; size < nearby::FRAME_BYTES; ++size) EXPECT_FALSE(nearby::decode(wire + 1, size, received));
  EXPECT_FALSE(nearby::decode(wire + 1, nearby::FRAME_BYTES + 1, received));
  wire[6] = 2;
  EXPECT_FALSE(nearby::decode(wire + 1, nearby::FRAME_BYTES, received));
}

TEST(WifiNearby, RejectsBadChecksumEmptyKeysAndZeroPort) {
  auto packet = nearbyPacket(7);
  uint8_t wire[nearby::FRAME_BYTES]{};
  ASSERT_TRUE(nearby::encode(packet, wire, sizeof(wire)));
  nearby::Packet received;
  wire[6] ^= 1;
  EXPECT_FALSE(nearby::decode(wire, sizeof(wire), received));
  ASSERT_TRUE(nearby::encode(packet, wire, sizeof(wire)));
  std::memset(wire + 44, 0, nearby::KEY_BYTES);
  EXPECT_FALSE(nearby::decode(wire, sizeof(wire), received));
  ASSERT_TRUE(nearby::encode(packet, wire, sizeof(wire)));
  wire[76] = wire[77] = 0;
  EXPECT_FALSE(nearby::decode(wire, sizeof(wire), received));
  packet = nearbyPacket(0);
  EXPECT_FALSE(nearby::encode(packet, wire, sizeof(wire)));
  packet = nearbyPacket(8);
  packet.address[37] ^= 1;
  EXPECT_FALSE(nearby::encode(packet, wire, sizeof(wire)));
}

TEST(WifiNearby, DeduplicatesByKeyRefreshesNospamAndIgnoresSelf) {
  nearby::Directory directory;
  const auto self = nearbyPacket(1);
  auto peer = nearbyPacket(2);
  EXPECT_FALSE(directory.seen(self.address, self.address, 0));
  ASSERT_TRUE(directory.seen(peer.address, self.address, 10));
  EXPECT_FALSE(directory.seen(peer.address, self.address, 20));
  EXPECT_EQ(directory.count, 1);
  peer.address[32] ^= 1;
  peer.address[36] ^= 1;
  EXPECT_TRUE(directory.seen(peer.address, self.address, 30));
  EXPECT_EQ(directory.count, 1);
  EXPECT_EQ(directory.peers[0].seenAt, 30u);
  EXPECT_EQ(std::memcmp(directory.peers[0].address, peer.address, nearby::ADDRESS_BYTES), 0);
  peer.address[0] ^= 1;
  EXPECT_FALSE(directory.seen(peer.address, self.address, 40));
}

TEST(WifiNearby, BoundedDirectoryExpiresAcrossClockWrapWithoutEvictingLivePeers) {
  nearby::Directory directory;
  const auto self = nearbyPacket(99);
  const uint32_t start = UINT32_MAX - 1000;
  for (uint8_t key = 1; key <= nearby::CAPACITY; ++key) {
    const auto peer = nearbyPacket(key);
    ASSERT_TRUE(directory.seen(peer.address, self.address, start));
  }
  const auto overflow = nearbyPacket(10);
  EXPECT_FALSE(directory.seen(overflow.address, self.address, start));
  const auto first = nearbyPacket(1);
  EXPECT_FALSE(directory.seen(first.address, self.address, start + 5000));
  EXPECT_FALSE(directory.expire(start + nearby::EXPIRY_MS - 1));
  ASSERT_TRUE(directory.expire(start + nearby::EXPIRY_MS));
  ASSERT_EQ(directory.count, 1);
  EXPECT_EQ(directory.peers[0].address[0], 1);
  EXPECT_TRUE(directory.seen(overflow.address, self.address, start + nearby::EXPIRY_MS));
  EXPECT_TRUE(directory.expire(start + nearby::EXPIRY_MS + 5000));
  ASSERT_EQ(directory.count, 1);
  EXPECT_EQ(directory.peers[0].address[0], 10);
}
