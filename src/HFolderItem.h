#ifndef __HFolderItem_H__
#define __HFolderItem_H__

#include "CLVEasyItem.h"
#include <Entry.h>
#include <String.h>
#include <Handler.h>
#include <List.h>
#include <OS.h>


enum{
	M_INVALIDATE = 'mINV'
};

enum{
	FOLDER_TYPE = 0,
	QUERY_TYPE,
	IMAP4_TYPE,
	SIMPLE_TYPE
};

class BListView;

class HMailItem;

class HFolderItem : public CLVEasyItem,public BHandler
{
public:
					HFolderItem(const entry_ref &ref,BListView *owner);
					HFolderItem(const char* name,int32 type,BListView *owner);
	virtual			~HFolderItem();
			
	virtual	void	StartGathering();
		BList*		MailList() {return &fMailList;}
			bool	IsReady()const {return (fThread == -1)?true:false;}		
			bool	IsDone()const {return fDone;}
			int32	Unread()const {return fUnread;}
			int32	CountMails() const {return fMailList.CountItems();}
	
		entry_ref	Ref(){return fFolderRef;}	
		node_ref	NodeRef() {return fNodeRef;}
	const char*		FolderName() {return fName.String();};
			void	SetName(int32 unread);
		
	virtual	void	StartRefreshCache();
			
			
			void	InvalidateMe();
			
			void	RemoveMails(BList* mails);
			void	AddMails(BList* mails);
			
			void	AddMail(HMailItem* mail);
			void	RemoveMail(HMailItem* mail);
		HMailItem*	RemoveMail(entry_ref& ref);
		HMailItem*	RemoveMail(node_ref& nref);
			
			int32	FolderType() const{return fFolderType;}
			void	EmptyMailList();	
			void	Launch();
			void	Gather();
	static 	int 	CompareItems(const CLVListItem *a_Item1, 
									const CLVListItem *a_Item2, 
									int32 KeyColumn);
	BListView*		Owner() const{return fOwner;}
	
			int32	ChildItems() const {return fChildItems;}
			void	IncreaseChildItemCount() {fChildItems++;}
			
			void	RemoveSettings();
protected:
			void	RefreshCache();	
			void	StartCreateCache(); // Create cache with thread
			void	RemoveCacheFile();
			void	CreateCache();
			void	AddMailsToCacheFile();
	
	static	int32 	ThreadFunc(void* data);
	static 	int		CompareFunc(const void* data1,const void* data2);
			
		status_t	ReadFromCache();
	static	int32	CreateCacheThread(void *data);
	static	int32	RefreshCacheThread(void *data);
	
	virtual void	MessageReceived(BMessage *message);
			void	NodeMonitor(BMessage *message);
			
			bool	IsSelected();
			
		HMailItem*	FindMail(node_ref &nref);
protected:
		BList		fMailList;
		bool		fDone;
		int32		fUnread;
		thread_id 	fThread;
		bool		fCancel;
		BString		fName;
		entry_ref  	fFolderRef;
		BListView	*fOwner;
private:
		thread_id	fCacheThread;
		bool		fCacheCancel;
		thread_id	fRefreshThread;
		int32		fFolderType;
		bool		fUseCache;
		node_ref	fNodeRef;
		int32		fChildItems;
};
#endif
