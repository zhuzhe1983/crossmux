#pragma once
#include <vector>

#include "activities/apps/chat/ChatModel.h"

namespace chat {
class Session {
 public:
  Snapshot state;
  bool storageWorks = true;
  std::vector<Message> receivedMessages;
  std::vector<Delivery> deliveries;
  uint8_t receipts = 0;
  void contact(const char* id, bool online) {
    auto& contact = state.contacts[state.contactCount++];
    std::strcpy(contact.id, id);
    contact.online = online;
  }
  bool received(const char* room, const uint8_t* text, size_t length, uint32_t id, const char* sender) {
    if (!storageWorks) return false;
    Message message;
    std::strcpy(message.peer, room);
    std::strcpy(message.sender, sender);
    std::memcpy(message.text, text, length);
    message.id = id;
    receivedMessages.push_back(message);
    return true;
  }
  void receipt(const char*, uint32_t, uint8_t count) { receipts = count; }
  void updateDelivery(const char*, uint32_t, Delivery value) { deliveries.push_back(value); }
  void status(Status value) { state.status = value; }
};
}  // namespace chat
