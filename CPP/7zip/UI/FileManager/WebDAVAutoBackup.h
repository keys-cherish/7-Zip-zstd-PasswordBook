// WebDAVAutoBackup.h

#ifndef ZIP7_INC_WEBDAV_AUTO_BACKUP_H
#define ZIP7_INC_WEBDAV_AUTO_BACKUP_H

#include "../../../Common/MyString.h"

namespace NWebDAVBackup
{

void TryAutoBackup(const FString &passwordBookPath);
void QueueAutoBackup(const FString &passwordBookPath);
bool RunManualBackupNow(UString *errorMessage = NULL);
bool RunManualRestoreNow(UString *errorMessage = NULL);
bool ImportPasswordBookBackupFile(const FString &backupFilePath, UString *errorMessage = NULL);
bool NormalizeWebDavConfigBasePath();

}

#endif
