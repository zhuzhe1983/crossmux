#pragma once

namespace crossstore {
// Call only from the foreground store/activity task after user confirmation.
// Both modes revoke permissions and join any owned background chat worker.
// includeData removes only this app's private data and, for the three chat
// IDs, their own transport data. Exported files and shared services survive.
// False may mean partial data cleanup; the CSP remains until cleanup succeeds
// so the user can retry. No multi-file atomic deletion guarantee is made.
bool uninstallPackage(const char* id, bool includeData);
}  // namespace crossstore
