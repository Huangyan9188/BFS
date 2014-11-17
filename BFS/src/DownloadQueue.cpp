/*
 * UploadQueue.cpp
 *
 *  Created on: 2014-07-17
 *      Author: Behrooz Shafiee Sarjaz
 */

#include "DownloadQueue.h"
#include "UploadQueue.h"
#include <thread>
#include "BackendManager.h"
#include "log.h"
#include "filesystem.h"
#include <iostream>
#include <mutex>
#include "MemoryController.h"

using namespace std;

namespace FUSESwift {

DownloadQueue::DownloadQueue():SyncQueue() {
	// TODO Auto-generated constructor stub

}

DownloadQueue::~DownloadQueue() {
}

bool DownloadQueue::shouldDownload(BackendItem item) {
  for(string toBeDelete:deletedFiles)
    if(toBeDelete == item.name)
      return false;
  //Check Size
  if(!MemoryContorller::getInstance().checkPossibility(item.length)){
    log_msg("Can't download %s, due to lack of space.\n",item.name.c_str());
    return false;
  }
  return true;
}

bool DownloadQueue::isDeleted(const std::string& _name) {
  for(string toBeDelete:deletedFiles)
    if(toBeDelete == _name)
      return true;
  return false;
}

void DownloadQueue::updateFromBackend() {
	//Try to query backend for list of files
	Backend* backend = BackendManager::getActiveBackend();
	if (backend == nullptr) {
		log_msg("No active backend for Download Queue\n");
		lock_guard<std::mutex> lock(deletedFilesMutex);
		deletedFiles.clear();
		return;
	}
	vector<BackendItem>* listFiles = backend->list();
	//Get lock for deleted files
	lock_guard<std::mutex> lock(deletedFilesMutex);
	if(listFiles == nullptr || listFiles->size() == 0) {
	  deletedFiles.clear();
	  if(listFiles!=nullptr){
	  	delete listFiles;
	  	listFiles = nullptr;
	  }
		return;
	}

	//Now we have actully some files to sync(download)
	for(auto it=listFiles->begin();it!=listFiles->end();it++) {
	  if(!shouldDownload(*it))
	    continue;
		push(new SyncEvent(SyncEventType::DOWNLOAD_CONTENT,nullptr,it->name));
		push(new SyncEvent(SyncEventType::DOWNLOAD_METADATA,nullptr,it->name));
		//log_msg("DOWNLOAD QUEUE: pushed %s Event.\n",it->c_str());
	}
	log_msg("DOWNLOAD QUEUE: Num of Events: %zu .\n",list.size());
	//Update list of files that were actually deleted
	for(auto it=deletedFiles.begin();it!=deletedFiles.end();) {
    bool exist = false;
    for(BackendItem fileItem:*listFiles) {
      if(fileItem.name == *it)
        exist = true;
    }
    if(!exist)
      it = deletedFiles.erase(it);
    else
      ++it;
  }
	//Release memory
	if(listFiles!=nullptr){
		delete listFiles;
		listFiles = nullptr;
	}
}

void DownloadQueue::addZooTask(vector<string>assignments) {
	//Try to query backend for list of files
	Backend* backend = BackendManager::getActiveBackend();
	if (backend == nullptr) {
		log_msg("No active backend for Download Queue\n");
		lock_guard<std::mutex> lock(deletedFilesMutex);
		deletedFiles.clear();
		return;
	}
	//Get lock for deleted files
	lock_guard<std::mutex> lock(deletedFilesMutex);
	if(assignments.size() == 0) {
	  deletedFiles.clear();
		return;
	}

	//Now we have actully some files to sync(download)
	for(auto it=assignments.begin();it!=assignments.end();it++) {
		for(string toBeDelete:deletedFiles)
			if(toBeDelete == *it)
				continue;
		push(new SyncEvent(SyncEventType::DOWNLOAD_CONTENT,nullptr,*it));
		push(new SyncEvent(SyncEventType::DOWNLOAD_METADATA,nullptr,*it));
		//log_msg("DOWNLOAD QUEUE: pushed %s Event.\n",it->c_str());
	}

	//Update list of files that were actually deleted
	for(auto it=deletedFiles.begin();it!=deletedFiles.end();) {
    bool exist = false;
    for(string fileItem:assignments) {
      if(fileItem == *it)
        exist = true;
    }
    if(!exist)
      it = deletedFiles.erase(it);
    else
      ++it;
  }
}

void DownloadQueue::processDownloadContent(const SyncEvent* _event) {
	if(_event == nullptr || _event->fullPathBuffer.length()==0)
		return;
	FileNode* fileNode = FileSystem::getInstance().getNode(_event->fullPathBuffer);
	//If File exist then we won't download it!
	if(fileNode!=nullptr)
	  return;
	//Ask backend to download the file for us
	Backend *backend = BackendManager::getActiveBackend();
	istream *iStream = backend->get(_event);
	if(iStream == nullptr) {
	  log_msg("Error in Downloading file:%s\n",_event->fullPathBuffer.c_str());
	  return;
	}
	//Now create a file in FS
	//handle directories
	FileNode* parent = FileSystem::getInstance().createHierarchy(_event->fullPathBuffer);
	string fileName = FileSystem::getInstance().getFileNameFromPath(_event->fullPathBuffer);
	FileNode *newFile = FileSystem::getInstance().mkFile(parent, fileName,false);
	newFile->lockDelete();
	newFile->open();
	newFile->unlockDelete();
	//Make a fake event to check if the file has been deleted
	//SyncEvent fakeDeleteEvent(SyncEventType::DELETE,nullptr,_event->fullPathBuffer);
	//and write the content
	char buff[FileSystem::blockSize];
	size_t offset = 0;
	while(iStream->eof() == false) {
	  iStream->read(buff,FileSystem::blockSize);
	  //CheckEvent validity
    //if(!UploadQueue::getInstance().checkEventValidity(fakeDeleteEvent)) break;;
	  if(newFile->mustBeDeleted())
	    break;
    //get lock delete so file won't be deleted
    newFile->lockDelete();
    int retCode = newFile->write(buff,offset,iStream->gcount());
    //Check space availability
	  if(retCode < 0) {
	    log_msg("Error in writing file:%s, probably no diskspace, Code:%d\n",newFile->getFullPath().c_str(),retCode);
	    newFile->close();
	    newFile->unlockDelete();
	    return;
	  }

	  newFile->unlockDelete();
	  offset += iStream->gcount();
	}
	//newFile->lockDelete();
	printf("DONWLOAD FINISHED:%s\n",newFile->getName().c_str());
	//newFile->unlockDelete();
	newFile->close();
}

void DownloadQueue::processDownloadMetadata(const SyncEvent* _event) {
}

DownloadQueue& DownloadQueue::getInstance() {
  //Static members
  static DownloadQueue instance;
  return instance;
}

void DownloadQueue::syncLoopWrapper() {
  DownloadQueue::getInstance().syncLoop();
}

void DownloadQueue::startSynchronization() {
  running = true;
	syncThread = new thread(syncLoopWrapper);
}

void DownloadQueue::stopSynchronization() {
  running = false;
}

void DownloadQueue::processEvent(const SyncEvent* _event) {
	Backend *backend = BackendManager::getActiveBackend();
	if (backend == nullptr) {
		log_msg("No active backend\n");
		return;
	}
	switch (_event->type) {
	case SyncEventType::DOWNLOAD_CONTENT:
		log_msg("Event:DOWNLOAD_CONTENT fullpath:%s\n",
				_event->fullPathBuffer.c_str());
		processDownloadContent(_event);
		break;
	case SyncEventType::DOWNLOAD_METADATA:
		log_msg("Event:DOWNLOAD_METADATA fullpath:%s\n",
				_event->fullPathBuffer.c_str());
		processDownloadMetadata(_event);
		break;
	default:
		log_msg("INVALID Event: file:%s TYPE:%S\n",
				_event->node->getFullPath().c_str(),
				SyncEvent::getEnumString(_event->type).c_str());
	}
}

void DownloadQueue::syncLoop() {
	const long maxDelay = 10000; //Milliseconds
	const long minDelay = 10; //Milliseconds
	long delay = 10; //Milliseconds

	while (running) {
		//Empty list
		if (!list.size()) {
			//log_msg("DOWNLOADQUEUE: I will sleep for %zu milliseconds\n", delay);
			this_thread::sleep_for(chrono::milliseconds(delay));
			delay *= 2;
			if (delay > maxDelay)
				delay = maxDelay;
			//Update list
			//updateFromBackend();
			continue;
		}
		//pop the first element and process it
		SyncEvent* event = pop();
		processEvent(event);
		//do cleanup! delete event
    if(event != nullptr)
      delete event;
    event = nullptr;
		//reset delay
		delay = minDelay;
	}
}

void DownloadQueue::informDeletedFiles(std::vector<std::string> list) {
  lock_guard<std::mutex> lock(deletedFilesMutex);
  deletedFiles.insert(deletedFiles.end(),list.begin(),list.end());
  /*for(string item:list)
    deletedFiles.push_back(item);*/
}

} /* namespace FUSESwift */