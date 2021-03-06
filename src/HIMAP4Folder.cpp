#include "HIMAP4Folder.h"
#include "HApp.h"
#include "HIMAP4Item.h"
#include "Encoding.h"
#include "HWindow.h"
#include "HFolderList.h"
#include "HString.h"
#include "Utilities.h"
#include "utf7.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Debug.h>
#include <stdlib.h>
#include <Path.h>
#include <FindDirectory.h>
#include <File.h>
#include <Directory.h>
#include <ListView.h>
#include <string.h>
#include <Autolock.h>

/***********************************************************
 * Constructor
 ***********************************************************/
HIMAP4Folder::HIMAP4Folder(const char* name,
						const char* folder_name,
						const char* server,
						int			port,
						const char*	login,
						const char* password,
						BListView *owner)
	:HFolderItem(name,IMAP4_TYPE,owner)
	,fClient(NULL)
	,fServer(server)
	,fPort(port)
	,fLogin(login)
	,fPassword(password)
	,fRemoteFolderPath(folder_name)
	,fFolderGathered(false)
	,fChildItem(false)
{
	SetAccountName(name);
}

/***********************************************************
 * Destructor
 ***********************************************************/
HIMAP4Folder::~HIMAP4Folder()
{
	StoreSettings();
	EmptyMailList();
	if(!IsChildFolder()&&fClient)
	{
		fClient->Logout();
		fClient->Close();
		delete fClient;
	}
}

/***********************************************************
 * StoreSettings
 ***********************************************************/
void
HIMAP4Folder::StoreSettings()
{
	if(fChildItem)
		return;
	BPath path;
	::find_directory(B_USER_SETTINGS_DIRECTORY,&path);
	path.Append(APP_NAME);
	path.Append("Accounts");
	::create_directory(path.Path(),0777);
	path.Append("IMAP4");
	::create_directory(path.Path(),0777);
	
	path.Append(fName.String());
	
	BFile file(path.Path(),B_WRITE_ONLY|B_CREATE_FILE);
	
	BMessage msg(B_SIMPLE_DATA);
	msg.AddString("folder",fRemoteFolderPath.String() );
	msg.AddString("server",fServer.String() );
	msg.AddInt16("port",fPort);
	msg.AddString("login",fLogin.String() );
	
	BString pass;
	int32 len = fPassword.Length();
	for(int32 i = 0;i < len;i++)
		pass += (char)255-fPassword[i];
	
	msg.AddString("password",pass.String() );	
	ssize_t size;
	msg.Flatten(&file,&size);
	file.SetSize(size);
}

/***********************************************************
 * StartRefreshCache
 ***********************************************************/
void
HIMAP4Folder::StartRefreshCache()
{
	PRINT(("Refresh IMAP4\n"));
	EmptyMailList();
	if(fThread != -1)
		return;
	fDone = false;
	// Set icon to open folder
	BBitmap *icon = ((HApp*)be_app)->GetIcon("CloseFolder");
	SetColumnContent(1,icon,2.0,false,false);
	
	StartGathering();
}

/***********************************************************
 * StartGathering
 ***********************************************************/
void
HIMAP4Folder::StartGathering()
{
	if(fThread != -1)
		return;
	//PRINT(("Query Gathering\n"));
	fThread = ::spawn_thread(GetListThread,"IMAP4Fetch",B_NORMAL_PRIORITY,this);
	::resume_thread(fThread);
}

/***********************************************************
 * GetListThread
 ***********************************************************/
int32
HIMAP4Folder::GetListThread(void *data)
{
	HIMAP4Folder *theItem = static_cast<HIMAP4Folder*>(data);
	theItem->IMAPGetList();
	return 0;
}

/***********************************************************
 * GetList
 ***********************************************************/
void
HIMAP4Folder::IMAPGetList()
{
	if(!fClient)
	{
		if(IMAPConnect() != B_OK)
		{
			if(fOwner->IndexOf(this) == fOwner->CurrentSelection())
				fOwner->Window()->PostMessage(M_STOP_MAIL_BARBER_POLE);
			return;	
		}
	}
	if(!fChildItem)
		GatherChildFolders();
	if(fRemoteFolderPath.Length() == 0)
	{
		if(fOwner->IndexOf(this) == fOwner->CurrentSelection())
				fOwner->Window()->PostMessage(M_STOP_MAIL_BARBER_POLE);
		return;
	}	
	
	int32 mail_count = 0;
	BAutolock lock(fClient);
	if((mail_count = fClient->Select(fRemoteFolderPath.String())) < 0)
	{
		(new BAlert("",_("Could not select remote folder"),_("OK")
						,NULL,NULL,B_WIDTH_AS_USUAL,B_STOP_ALERT))->Go();
		if(fOwner->IndexOf(this) == fOwner->CurrentSelection())
				fOwner->Window()->PostMessage(M_STOP_MAIL_BARBER_POLE);	
		return;
	}
	
	BString subject,from,to,cc,reply,date,priority;
	bool read,attachment;
	Encoding encode;
	
	for(int32 i = 1;i <= mail_count;i++)
	{
		subject = from = to = date = priority = "";
		if(fClient->FetchFields(i,
								subject,
								from,
								to,
								cc,
								reply,
								date,
								priority,
								read,
								attachment) != B_OK)
		{
			PRINT(("FetchFields ERROR\n"));
			continue;
		}
		PRINT(("%s\n",date.String()));
		// ConvertToUTF8
		encode.Mime2UTF8(subject);
		encode.Mime2UTF8(from);
		encode.Mime2UTF8(to);
		//
		fMailList.AddItem(new HIMAP4Item( (read)?"Read":"New",	
											subject.String(),
											from.String(),
											to.String(),
											cc.String(),
											reply.String(),
											MakeTime_t(date.String()),
											priority.String(),
											(attachment)?1:0,
											i,
											fClient,this
											));
		if(!read) fUnread++;
	}
	SetUnreadCount(fUnread);
	
	fDone = true;
	
	// Set icon to open folder
	BBitmap *icon = ((HApp*)be_app)->GetIcon("OpenIMAP");
	SetColumnContent(1,icon,2.0,false,false);
	
	InvalidateMe();
	fThread = -1;
	return;
}

/***********************************************************
 * Connect
 ***********************************************************/
status_t
HIMAP4Folder::IMAPConnect()
{
	delete fClient;
	fClient = new IMAP4Client();
	PRINT(("IMAP4 Connect Start:%s %d\n",fServer.String(),fPort));
	if( fClient->Connect(fServer.String(),fPort) != B_OK)
	{
		Alert(B_STOP_ALERT,"%s\nAddress:%s Port:%d",_("Could not connect to IMAP4 server"),fServer.String(),fPort);
		delete fClient;
		fClient = NULL;
		return B_ERROR;
	}
	if( fClient->Login(fLogin.String(),fPassword.String()) != B_OK)
	{
		Alert(B_STOP_ALERT,_("Could not login to IMAP4 server"));
		delete fClient;
		fClient = NULL;	
		return B_ERROR;
	}
	return B_OK;
}


/***********************************************************
 * GatherChildFolders
 ***********************************************************/
void
HIMAP4Folder::GatherChildFolders()
{
	if(fFolderGathered)
		return;
	BList namelist,pointerList;
	
	int32 count = fClient->List(fRemoteFolderPath.String(),&namelist);
	if(count <= 0)
		return;
	
	BMessage childMsg(M_ADD_UNDER_ITEM);
	char displayName[B_FILE_NAME_LENGTH];
	char *p;
		
	HIMAP4Folder *folder;
	
	for(int32 i = 0;i < count;i++)
	{
		char *name = (char*)namelist.ItemAt(i);
		if(strstr(name,"/"))
		{
			int32 namelen = ::strlen(name);
			p = name;
			p += namelen-1;
			while(*p != '/')
				p--;
			p++;
		}
		else
			p = name;
		::strcpy(displayName,p);
		displayName[::strlen(p)] = '\0';
		// Convert UTF7 to UTF8
		char *buf = new char[strlen(displayName)*4];
		IMAP4UTF72UTF8(buf,displayName);
		
		pointerList.AddItem((folder = MakeNewFolder(buf,(char*)namelist.ItemAt(i) )));
		delete[] buf;
		free( name );
	}
	
	int32 index;
	for(int32 i = 0;i < count;i++)
	{
		folder = (HIMAP4Folder*)pointerList.ItemAt(i);
		index = FindParent(folder->FolderName(),folder->RemoteFolderPath(),&pointerList);
		childMsg.AddPointer("parent",(index < 0)?this:(HIMAP4Folder*)pointerList.ItemAt(index));
		childMsg.AddBool("expand",(index < 0)?true:false);
		childMsg.AddPointer("item",folder);
	}
	if(!childMsg.IsEmpty())
		fOwner->Window()->PostMessage(&childMsg,fOwner);
	fFolderGathered = true;
}

/***********************************************************
 * FindParent
 ***********************************************************/
int32
HIMAP4Folder::FindParent(const char* name,const char* folder_path,BList *list)
{
	int32 count = list->CountItems();
	HIMAP4Folder *folder;
	char path[B_PATH_NAME_LENGTH];
	
	if(::strstr(folder_path,"/"))
	{
		::strcpy(path,folder_path);
		path[::strlen(path)-::strlen(name) -1] = '\0';
	}else
		return -1; 
	
	for(int32 i = 0;i < count;i++)
	{
		folder = (HIMAP4Folder*)list->ItemAt(i);
		if(strcmp(folder->RemoteFolderPath(),path) == 0)
			return i;
	}
	return -1;
}

/***********************************************************
 * DeleteMe
 ***********************************************************/
bool
HIMAP4Folder::DeleteMe()
{
	if(fClient->Delete(fRemoteFolderPath.String()) == B_OK)
		return true;
	(new BAlert("",_("Could not delete the folder."),_("OK"),NULL,NULL,B_WIDTH_AS_USUAL,B_STOP_ALERT))->Go();
	return false;
}

/***********************************************************
 * CreateChildFolder
 ***********************************************************/
void
HIMAP4Folder::CreateChildFolder(const char* utf8)
{
	if(fClient->Create(utf8,fRemoteFolderPath.String()) == B_OK)	
	{
		char *utf7 = new char[strlen(utf8)*4];
		UTF8IMAP4UTF7(utf7,(char*)utf8);
		BString path;
		if(IsChildFolder())
			path+=fRemoteFolderPath;
		path+=utf7;
		delete[] utf7;
		
		HIMAP4Folder *folder = MakeNewFolder(utf8,path.String());
		
		BMessage childMsg(M_ADD_UNDER_ITEM);
		childMsg.AddPointer("item",folder);
		childMsg.AddPointer("parent",this);
		childMsg.AddBool("expand",false);
		fOwner->Window()->PostMessage(&childMsg,fOwner);
	}
}

/***********************************************************
 * Move
 ***********************************************************/
status_t
HIMAP4Folder::Move(const char* indexList,const char* dest_folder)
{
	fClient->Select(RemoteFolderPath());
	return fClient->Copy(indexList,dest_folder);
}

/***********************************************************
 * MakeNewFolder
 ***********************************************************/
HIMAP4Folder*
HIMAP4Folder::MakeNewFolder(const char* utf8name,const char* utf7path)
{
	HIMAP4Folder *folder = new HIMAP4Folder(utf8name
									,utf7path
									,fServer.String()
									,fPort
									,fLogin.String()
									,fPassword.String()
									,fOwner);
	folder->SetAccountName(AccountName());
	folder->SetChildFolder(true);
	folder->SetFolderGathered(true); // not need to gather child folders.
	folder->fClient = fClient;
	return folder;
}
