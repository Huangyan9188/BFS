/*
 * DownlaodQueue.h
 *
 *  Created on: 2014-07-17
 *      Author: Behrooz Shafiee Sarjaz
 */

#ifndef DOWNLOADQUEUE_H_
#define DOWNLOADQUEUE_H_
#include "SyncQueue.h"

namespace FUSESwift {

class DownloadQueue: public SyncQueue{
  //Singleton instance
  static DownloadQueue* mInstance;
  //Process Events
  void processEvent(const SyncEvent* _event);
  static void syncLoopWrapper();
  void syncLoop();
  void updateFromBackend();
  void processDownloadContent(const SyncEvent* _event);
  void processDownloadMetadata(const SyncEvent* _event);
  //Delete files
  std::vector<std::string> deletedFiles;
  bool shouldDownload(std::string);
  //Private constructor
  DownloadQueue();
public:
  static DownloadQueue& getInstance();
  virtual ~DownloadQueue();
  //Start Downlaod Thread
  void startSynchronization();
  //Stop Downlaod Thread
  void stopSynchronization();
  //Inform deleted files, so I won't downlaod them while server is removing them.
  void informDeletedFiles(std::vector<std::string>);
};

} /* namespace FUSESwift */
#endif /* DOWNLOADQUEUE_H_ */