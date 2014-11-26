
#include <ptlib.h>
#include <stdio.h>
#include <string.h>

#include "conference.h"
#include "mcu.h"

#if MCU_VIDEO
#include <ptlib/vconvert.h>
#endif

// size of a PCM data packet, in samples
//#define PCM_PACKET_LEN          480
//#define PCM_PACKET_LEN          1920
#define PCM_BUFFER_LEN_MS /*ms */ 120

// size of a PCM data buffer, in bytes
//#define PCM_BUFFER_LEN          (PCM_PACKET_LEN * 2)

// number of PCM buffers to keep
#define PCM_BUFFER_COUNT        2

#define PCM_BUFFER_SIZE_CALC(freq,chans)\
  bufferSize = 2/* bytes*/ * chans * PCM_BUFFER_LEN_MS * PCM_BUFFER_COUNT * freq / 1000;\
  if(bufferSize < 4) bufferSize=200;\
  buffer.SetSize(bufferSize + 16);


//#define PCM_BUFFER_SIZE         (PCM_BUFFER_LEN * PCM_BUFFER_COUNT)

////////////////////////////////////////////////////////////////////////////////////

ConferenceManager::ConferenceManager()
{
  maxConferenceCount = 0;
  monitor  = new ConferenceMonitor(*this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceManager::~ConferenceManager()
{
  monitor->running = FALSE;
  monitor->WaitForTermination();
  delete monitor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::FindConferenceWithLock(const OpalGloballyUniqueID & conferenceID)
{
  PWaitAndSignal m(conferenceListMutex);
  Conference *conference = FindConferenceWithoutLock(conferenceID);
  if(conference)
    conference->Lock();
  return conference;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::FindConferenceWithoutLock(const OpalGloballyUniqueID & conferenceID)
{
  PWaitAndSignal m(conferenceListMutex);
  ConferenceListType::const_iterator r = conferenceList.find(conferenceID);
  if(r != conferenceList.end())
    return r->second;
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::FindConferenceWithLock(const PString & room)
{
  if(room.IsEmpty())
    return NULL;
  PWaitAndSignal m(conferenceListMutex);
  Conference *conference = FindConferenceWithoutLock(room);
  if(conference)
    conference->Lock();
  return conference;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::FindConferenceWithoutLock(const PString & room)
{
  if(room.IsEmpty())
    return NULL;
  PWaitAndSignal m(conferenceListMutex);
  for(ConferenceListType::const_iterator r = conferenceList.begin(); r != conferenceList.end(); ++r)
  {
    if(r->second->GetNumber() == room)
      return r->second;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::FindConferenceWithLock(Conference * conference)
{
  if(conference == NULL)
    return FALSE;
  PWaitAndSignal m(conferenceListMutex);
  for(ConferenceListType::const_iterator r = conferenceList.begin(); r != conferenceList.end(); ++r)
  {
    if(r->second == conference)
    {
      conference->Lock();
      return conference;
    }
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::MakeConferenceWithLock(const PString & room, PString name)
{
  PWaitAndSignal m(conferenceListMutex);
  Conference * conference = MakeConferenceWithoutLock(room, name);
  if(conference)
    conference->Lock();
  return conference;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::MakeConferenceWithoutLock(const PString & room, PString name)
{
  PWaitAndSignal m(conferenceListMutex);
  Conference * conference = FindConferenceWithoutLock(room);
  if(conference == NULL)
  {
    // create the conference
    OpalGloballyUniqueID conferenceID;
    conference = CreateConference(conferenceID, room, name, mcuNumberMap.GetNumber(conferenceID));
    // insert conference into the map
    conferenceList.insert(ConferenceListType::value_type(conferenceID, conference));
    // set the conference count
    maxConferenceCount = PMAX(maxConferenceCount, (PINDEX)conferenceList.size());
    OnCreateConference(conference);
  }
  return conference;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL ConferenceManager::HasConference(const PString & number, OpalGloballyUniqueID & conferenceID)
{
  Conference *conference = FindConferenceWithLock(number);
  if(conference)
  {
    conferenceID = conference->GetID();
    conference->Unlock();
    return TRUE;
  }
  return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL ConferenceManager::HasConference(const OpalGloballyUniqueID & conferenceID, PString & number)
{
  Conference *conference = FindConferenceWithLock(conferenceID);
  if(conference)
  {
    number = conference->GetNumber();
    conference->Unlock();
    return TRUE;
  }
  return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceProfile * ConferenceManager::FindProfileWithLock(const PString & roomName, const PString & memberName)
{
  Conference *conference = FindConferenceWithLock(roomName);
  if(conference == NULL)
    return NULL;
  ConferenceProfile *profile = FindProfileWithLock(conference, memberName);
  conference->Unlock();
  return profile;
}
ConferenceProfile * ConferenceManager::FindProfileWithLock(Conference * conference, const PString & memberName)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  ConferenceProfile *profile = FindProfileWithoutLock(conference, memberName);
  if(profile)
    profile->Lock();
  return profile;
}
ConferenceProfile * ConferenceManager::FindProfileWithoutLock(Conference * conference, const PString & memberName)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  Conference::ProfileList & profileList = conference->GetProfileList();
  for(Conference::ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    ConferenceProfile *profile = r->second;
    if(profile->GetName() == memberName)
      return profile;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceMember * ConferenceManager::FindMemberWithLock(const PString & roomName, const PString & memberName)
{
  Conference *conference = FindConferenceWithLock(roomName);
  if(conference == NULL)
    return NULL;
  ConferenceMember *member = FindMemberWithLock(conference, memberName);
  conference->Unlock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberWithLock(Conference * conference, const PString & memberName)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  ConferenceMember *member = FindMemberWithoutLock(conference, memberName);
  if(member)
    member->Lock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberWithoutLock(Conference * conference, const PString & memberName)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  Conference::ProfileList & profileList = conference->GetProfileList();
  for(Conference::ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    ConferenceMember *member = r->second->GetMember();
    if(member && member->GetName() == memberName)
      return member;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceMember * ConferenceManager::FindMemberNameIDWithLock(const PString & roomName, const PString & memberName)
{
  Conference *conference = FindConferenceWithLock(roomName);
  if(conference == NULL)
    return NULL;
  ConferenceMember *member = FindMemberWithLock(conference, memberName);
  conference->Unlock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberNameIDWithLock(Conference * conference, const PString & memberName)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  ConferenceMember *member = FindMemberWithoutLock(conference, memberName);
  if(member)
    member->Lock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberNameIDWithoutLock(Conference * conference, const PString & memberName)
{
  PString memberNameID = MCUURL(memberName).GetMemberNameId();
  PWaitAndSignal m(conference->GetProfileListMutex());
  Conference::ProfileList & profileList = conference->GetProfileList();
  for(Conference::ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    ConferenceMember *member = r->second->GetMember();
    if(member && member->GetNameID() == memberNameID)
      return member;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceMember * ConferenceManager::FindMemberWithLock(const PString & roomName, long id)
{
  Conference *conference = FindConferenceWithLock(roomName);
  if(conference == NULL)
    return NULL;
  ConferenceMember *member = FindMemberWithLock(conference, id);
  conference->Unlock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberWithLock(Conference * conference, long id)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  ConferenceMember *member = FindMemberWithoutLock(conference, id);
  if(member)
    member->Lock();
  return member;
}
ConferenceMember * ConferenceManager::FindMemberWithoutLock(Conference * conference, long id)
{
  PWaitAndSignal m(conference->GetProfileListMutex());
  Conference::ProfileList & profileList = conference->GetProfileList();
  for(Conference::ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    ConferenceMember *member = r->second->GetMember();
    if(member && (long)member->GetID() == id)
      return member;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

MCUSimpleVideoMixer * ConferenceManager::FindMixerWithLock(const PString & roomName, long id)
{
  Conference *conference = FindConferenceWithLock(roomName);
  if(conference == NULL)
    return NULL;
  MCUSimpleVideoMixer * mixer = FindMixerWithLock(conference, id);
  conference->Unlock();
  return mixer;
}
MCUSimpleVideoMixer * ConferenceManager::FindMixerWithLock(Conference * conference, long id)
{
  if(conference->videoMixerList)
  {
    PWaitAndSignal m(conference->videoMixerListMutex);
    MCUSimpleVideoMixer *mixer = FindMixerWithoutLock(conference, id);
    if(mixer)
      mixer->Lock();
    return mixer;
  } else {
    PWaitAndSignal m(conference->GetProfileListMutex());
    MCUSimpleVideoMixer *mixer = FindMixerWithoutLock(conference, id);
    if(mixer)
      mixer->Lock();
    return mixer;
  }
  return NULL;
}
MCUSimpleVideoMixer * ConferenceManager::FindMixerWithoutLock(Conference * conference, long id)
{
  if(conference->videoMixerList)
  {
    PWaitAndSignal m(conference->videoMixerListMutex);
    MCUVideoMixer *_mixer = conference->VMLFind(id);
    MCUSimpleVideoMixer *mixer = dynamic_cast<MCUSimpleVideoMixer *>(_mixer);
    return mixer;
  } else {
    ConferenceMember *member = FindMemberWithLock(conference, id);
    if(member)
    {
      MCUVideoMixer *_mixer = member->videoMixer;
      MCUSimpleVideoMixer *mixer = dynamic_cast<MCUSimpleVideoMixer *>(_mixer);
      member->Unlock();
      return mixer;
    }
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferenceManager::OnCreateConference(Conference * conference)
{
  // add monitor event
  monitor->AddMonitorEvent(new ConferenceStatusInfo(conference->GetID()));
  monitor->AddMonitorEvent(new ConferenceRecorderInfo(conference->GetID()));
  int timeLimit = GetConferenceParam(conference->GetNumber(), RoomTimeLimitKey, 0);
  if(timeLimit > 0)
    monitor->AddMonitorEvent(new ConferenceTimeLimitInfo(conference->GetID(), PTime() + timeLimit*1000));

  if(MCUConfig("Export Parameters").GetBoolean("Enable export", FALSE) == TRUE)
    conference->pipeMember = new ConferencePipeMember(conference);

  // add file recorder member
  if(GetConferenceParam(conference->GetNumber(), RoomAllowRecordKey, TRUE))
  {
    conference->conferenceRecorder = new ConferenceRecorder(conference);
  }

  if(!conference->GetForceScreenSplit())
  { PTRACE(1,"Conference\tOnCreateConference: \"Force split screen video\" unchecked, " << conference->GetNumber() << " skipping members.conf"); return; }

  FILE *membLst;

  // read members.conf into conference->membersConf
  membLst = fopen(PString(SYS_CONFIG_DIR) + PATH_SEPARATOR + "members_" + conference->GetNumber() + ".conf","rt");
  PStringStream membersConf;
  if(membLst!=NULL)
  { char buf [128];
    while(fgets(buf, 128, membLst)!=NULL) membersConf << buf;
    fclose(membLst);
  }

  conference->membersConf=membersConf;
  if(membersConf.Left(1)!="\n") membersConf="\n"+membersConf;

  // recall last template
  if(!GetConferenceParam(conference->GetNumber(), RoomRecallLastTemplateKey, FALSE)) return;

  PINDEX dp=membersConf.Find("\nLAST_USED ");
  if(dp!=P_MAX_INDEX)
  { PINDEX dp2=membersConf.Find('\n',dp+10);
    if(dp2!=P_MAX_INDEX)
    { PString lastUsedTemplate=membersConf.Mid(dp+11,dp2-dp-11).Trim();
      PTRACE(4, "Extracting & loading last used template: " << lastUsedTemplate);
      conference->confTpl=conference->ExtractTemplate(lastUsedTemplate);
      conference->LoadTemplate(conference->confTpl);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferenceManager::OnDestroyConference(Conference * conference)
{
  PString number = conference->GetNumber();
  PTRACE(2,"MCU\tOnDestroyConference " << number);
  conference->stopping=TRUE;

  PString jsName(number);
  jsName.Replace("\"","\\x27",TRUE,0); jsName.Replace("'","\\x22",TRUE,0);
  OpenMCU::Current().HttpWriteCmdRoom("notice_deletion(1,'" + jsName + "')", number);

  conference->StopRecorder();

  // delete profiles
  conference->GetProfileListMutex().Wait();
  for(Conference::ProfileList::iterator r = conference->GetProfileList().begin(); r != conference->GetProfileList().end(); ++r)
  {
    ConferenceProfile *profile = r->second;
    conference->GetProfileList().erase(r->first);
    profile->Lock();
    delete profile;
  }
  conference->GetProfileListMutex().Signal();

  PTRACE(2,"MCU\tOnDestroyConference " << number <<", disconnect remote endpoints");
  conference->GetMemberListMutex().Wait();
  for(Conference::MemberList::iterator r = conference->GetMemberList().begin(); r != conference->GetMemberList().end(); ++r)
  {
    ConferenceMember * member = r->second;
    if(member)
    {
      member->SetConference(NULL); // prevent further attempts to read audio/video data from conference
      if(member->GetType() != MEMBER_TYPE_PIPE && member->GetType() != MEMBER_TYPE_CACHE)
        member->Close();
    }
  }
  conference->GetMemberListMutex().Signal();

  OpenMCU::Current().HttpWriteCmdRoom("notice_deletion(2,'" + jsName + "')", number);

  for(PINDEX i = 0; i < 100; i++)
  {
   PThread::Sleep(100);
   if(!conference->GetVisibleMemberCount())
     break;
  }

  PTRACE(2,"MCU\tOnDestroyConference " << number <<", remove pipes and caches");
  OpenMCU::Current().HttpWriteCmdRoom("notice_deletion(3,'" + jsName + "')", number);
  conference->GetMemberListMutex().Wait();
  for(Conference::MemberList::iterator r = conference->GetMemberList().begin(); r != conference->GetMemberList().end(); ++r)
  {
    ConferenceMember * member = r->second;
    if(member)
    {
      if(member->GetType() == MEMBER_TYPE_PIPE)
        delete (ConferencePipeMember *)member;
      else if(member->GetType() == MEMBER_TYPE_CACHE)
        delete (ConferenceCacheMember *)member;
    }
  }
//  OpenMCU::Current().HttpWriteCmdRoom("notice_deletion(4,'" + jsName + "')", number);

  if(conference->conferenceRecorder)
    delete conference->conferenceRecorder;

  conference->GetMemberListMutex().Signal();
  OpenMCU::Current().HttpWriteCmdRoom("notice_deletion(5,'" + jsName + "')", number);
}


////////////////////////////////////////////////////////////////////////////////////////////////////

Conference * ConferenceManager::CreateConference(const OpalGloballyUniqueID & _guid,
                                                              const PString & _number,
                                                              const PString & _name,
                                                              int _mcuNumber)
{
#if MCU_VIDEO
#  if ENABLE_ECHO_MIXER
     if(_number.Left(4) *= "echo")
       return new Conference(*this, _guid, "echo"+_guid.AsString(), _name, _mcuNumber, new EchoVideoMixer());
#  endif
#  if ENABLE_TEST_ROOMS
     if(_number.Left(8) == "testroom")
     {
       PString number = _number;
       int count = 0;
       if(_number.GetLength() > 8)
       {
         count = _number.Mid(8).AsInteger();
         if(count <= 0) { count = 0; number = "testroom"; }
       }
       if(count >= 0)
         return new Conference(*this, _guid, number, _name, _mcuNumber, new TestVideoMixer(count));
     }
#  endif
#endif

  BOOL forceScreenSplit = GetConferenceParam(_number, ForceSplitVideoKey, TRUE);
  //BOOL forceScreenSplit = TRUE;

  if(!forceScreenSplit)
  {
    Conference *conference = new Conference(*this, _guid, _number, _name, _mcuNumber, NULL);
    return conference;
  }

  PINDEX slashPos = _number.Find('/');
  PString number;
  if (slashPos != P_MAX_INDEX) number=_number.Left(slashPos);
  else number=_number;

  return new Conference(*this, _guid, number, _name, _mcuNumber
#if MCU_VIDEO
                        ,OpenMCU::Current().CreateVideoMixer(forceScreenSplit)
#endif
                        ); 
}

void ConferenceManager::RemoveConference(const OpalGloballyUniqueID & confId)
{
  PWaitAndSignal m(conferenceListMutex);
  ConferenceListType::iterator r = conferenceList.find(confId);
  if(r != conferenceList.end())
  {
    Conference * conf = r->second;
    conf->Lock();
    conferenceList.erase(r);
    mcuNumberMap.RemoveNumber(conf->GetMCUNumber());
    OnDestroyConference(conf);
    //monitor->RemoveForConference(conf->GetID());
    delete conf;
    PTRACE(1, "RemoveConference");
  }
}

void ConferenceManager::ClearConferenceList()
{
  PWaitAndSignal m(conferenceListMutex);
  for(ConferenceListType::iterator r = conferenceList.begin(); r != conferenceList.end(); )
  {
    Conference * conf = r->second;
    conf->Lock();
    conferenceList.erase(r++);
    mcuNumberMap.RemoveNumber(conf->GetMCUNumber());
    OnDestroyConference(conf);
    delete conf;
    PTRACE(1, "RemoveConference");
  }
}

void ConferenceManager::ClearMonitorEvents()
{
  PWaitAndSignal m(conferenceListMutex);
  for(ConferenceListType::iterator r = conferenceList.begin(); r != conferenceList.end(); ++r)
    monitor->RemoveForConference(r->second->GetID());
}

void ConferenceManager::AddMonitorEvent(ConferenceMonitorInfo * info)
{
  monitor->AddMonitorEvent(info);
}

///////////////////////////////////////////////////////////////

void ConferenceMonitor::Main()
{
  running = TRUE;

  for (;;) {

    if (!running)
      break;

    Sleep(1000);

    if (!running)
      break;

    PWaitAndSignal m(mutex);

    PTime now;
    MonitorInfoList::iterator r = monitorList.begin();
    while (r != monitorList.end()) {
      ConferenceMonitorInfo & info = **r;
      if (now < info.timeToPerform)
        ++r;
      else {
        BOOL deleteAfterPerform = TRUE;
        {
          PWaitAndSignal m2(manager.GetConferenceListMutex());
          ConferenceListType & confList = manager.GetConferenceList();
          ConferenceListType::iterator s = confList.find(info.guid);
          if (s != confList.end())
            deleteAfterPerform = info.Perform(*s->second);
        }
        if (!deleteAfterPerform)
          ++r;
        else {
          delete *r;
          monitorList.erase(r);
          r = monitorList.begin();
        }
      }
    }
  }
}

void ConferenceMonitor::AddMonitorEvent(ConferenceMonitorInfo * info)
{
  PWaitAndSignal m(mutex);
  monitorList.push_back(info);
}

void ConferenceMonitor::RemoveForConference(const OpalGloballyUniqueID & guid)
{
  PWaitAndSignal m(mutex);
  MonitorInfoList::iterator r = monitorList.begin();
  while (r != monitorList.end()) {
    ConferenceMonitorInfo & info = **r;
    if (info.guid != guid)
      ++r;
    else {
      delete *r;
      monitorList.erase(r);
      r = monitorList.begin();
    }
  }
}

BOOL ConferenceTimeLimitInfo::Perform(Conference & conference)
{
  ConferenceManager & cm = OpenMCU::Current().GetEndpoint().GetConferenceManager();
  cm.RemoveConference(conference.GetID());
  return FALSE;
}

BOOL ConferenceRepeatingInfo::Perform(Conference & conference)
{
  this->timeToPerform = PTime() + repeatTime;
  return FALSE;
}

BOOL ConferenceStatusInfo::Perform(Conference & conference)
{
  // auto delete empty room
  BOOL autoDeleteEmpty = GetConferenceParam(conference.GetNumber(), RoomAutoDeleteEmptyKey, FALSE);
  if(autoDeleteEmpty && !conference.GetVisibleMemberCount())
  {
    ConferenceManager & cm = OpenMCU::Current().GetEndpoint().GetConferenceManager();
    cm.RemoveConference(conference.GetID());
    return TRUE; // delete monitor
  }

  return ConferenceRepeatingInfo::Perform(conference);
}

BOOL ConferenceRecorderInfo::Perform(Conference & conference)
{
  // recorder
  BOOL allowRecord = GetConferenceParam(conference.GetNumber(), RoomAllowRecordKey, TRUE);
  if(!allowRecord)
  {
    conference.StopRecorder();
    return TRUE; // delete monitor
  }

  PString autoRecordStart = GetConferenceParam(conference.GetNumber(), RoomAutoRecordStartKey, "Disable");
  PString autoRecordStop = GetConferenceParam(conference.GetNumber(), RoomAutoRecordStopKey, "Disable");

  PINDEX visibleMembers = conference.GetVisibleMemberCount();

  if(autoRecordStop != "Disable" && visibleMembers <= autoRecordStop.AsInteger())
    conference.StopRecorder();
  else if(autoRecordStart != "Disable" && autoRecordStart.AsInteger() > autoRecordStop.AsInteger() && visibleMembers >= autoRecordStart.AsInteger())
    conference.StartRecorder();

  return ConferenceRepeatingInfo::Perform(conference);
}

BOOL ConferenceMCUCheckInfo::Perform(Conference & conference)
{
  // see if any member of this conference is a not an MCU
  Conference::MemberList & list = conference.GetMemberList();
  Conference::MemberList::iterator r;
  for (r = list.begin(); r != list.end(); ++r)
    if (r->second->GetType() != MEMBER_TYPE_MCU)
      break;

  // if there is any non-MCU member, check again later
  if (r != list.end())
    return ConferenceRepeatingInfo::Perform(conference);

  // else shut down the conference
  for (r = list.begin(); r != list.end(); ++r)
    r->second->Close();

  return TRUE;
}


////////////////////////////////////////////////////////////////////////////////////

Conference::Conference(        ConferenceManager & _manager,
                       const OpalGloballyUniqueID & _guid,
                                    const PString & _number,
                                    const PString & _name,
                                                int _mcuNumber
#if MCU_VIDEO
                                    ,MCUVideoMixer * _videoMixer
#endif
)
  : manager(_manager), guid(_guid), number(_number), name(_name), mcuNumber(_mcuNumber), mcuMonitorRunning(FALSE)
{
  stopping = FALSE;
#if MCU_VIDEO
  VMLInit(_videoMixer);
#endif
  maxMemberCount = 0;
  moderated = FALSE;
  muteUnvisible = FALSE;
  VAdelay = 1000;
  VAtimeout = 10000;
  VAlevel = 100;
  echoLevel = 0;
  vidmembernum = 0;
  conferenceRecorder = NULL;
  forceScreenSplit = GetConferenceParam(number, ForceSplitVideoKey, TRUE);
  lockedTemplate = GetConferenceParam(number, LockTemplateKey, FALSE);
  pipeMember = NULL;
  PTRACE(3, "Conference\tNew conference started: ID=" << guid << ", number = " << number);
}

Conference::~Conference()
{
#if MCU_VIDEO
  VMLClear();
#endif
}

BOOL Conference::RecorderCheckSpace()
{
  PDirectory pd(OpenMCU::Current().vr_ffmpegDir);
  PInt64 t, f;
  DWORD cs;
  if(!pd.GetVolumeSpace(t, f, cs))
  {
    PTRACE(1,"EVRT\tRecorder space check failed");
    return TRUE;
  }
  BOOL result = ((f>>20) >= OpenMCU::Current().vr_minimumSpaceMiB);
  if(!result) OpenMCU::Current().HttpWriteEvent("<b><font color='red'>Insufficient disk space</font>: Video Recorder DISABLED</b>");
  PTRACE_IF(1,!result,"EVRT\tInsufficient disk space: Video Recorder DISABLED");
  return result;
}

BOOL Conference::StartRecorder()
{
  if(!conferenceRecorder)
    return FALSE;
  if(conferenceRecorder->IsRunning())
    return TRUE;

  if(!RecorderCheckSpace())
    return FALSE;
  if(!PDirectory::Exists(OpenMCU::Current().vr_ffmpegDir))
  {
    PTRACE(1,"EVRT\tRecorder failed to start (check recorder directory)");
    OpenMCU::Current().HttpWriteEventRoom("Recorder failed to start (check recorder directory)", number);
    return FALSE;
  }

  conferenceRecorder->Start();
  for(int i = 0; i < 10; i++) if(conferenceRecorder->IsRunning()) break; else PThread::Sleep(100);
  if(!conferenceRecorder->IsRunning())
  {
    PTRACE(1,"MCU\tConference: " << number <<", failed to start recorder");
    return FALSE;
  }

  PTRACE(1,"MCU\tConference: " << number <<", video recorder started");
  OpenMCU::Current().HttpWriteEventRoom("video recording started", number);
  OpenMCU::Current().HttpWriteCmdRoom(OpenMCU::Current().GetEndpoint().GetConferenceOptsJavascript(*this), number);
  OpenMCU::Current().HttpWriteCmdRoom("build_page()", number);
  return TRUE;
}

BOOL Conference::StopRecorder()
{
  if(!conferenceRecorder)
    return TRUE;
  if(!conferenceRecorder->IsRunning())
    return TRUE;
  conferenceRecorder->Stop();

  PTRACE(1,"MCU\tConference: " << number <<", video recorder stopped");
  OpenMCU::Current().HttpWriteEventRoom("video recording stopped", number);
  OpenMCU::Current().HttpWriteCmdRoom(OpenMCU::Current().GetEndpoint().GetConferenceOptsJavascript(*this), number);
  OpenMCU::Current().HttpWriteCmdRoom("build_page()", number);
  return TRUE;
}

int Conference::GetVisibleMemberCount() const
{
  PWaitAndSignal m(memberListMutex);
  int visibleMembers = 0;
  std::map<void *, ConferenceMember *>::const_iterator r;
  for (r = memberList.begin(); r != memberList.end(); r++) {
    if (r->second->IsVisible())
      ++visibleMembers;
  }
  return visibleMembers;
}

void Conference::AddMonitorEvent(ConferenceMonitorInfo * info)
{ 
  manager.AddMonitorEvent(info); 
}

void Conference::RefreshAddressBook()
{
  PStringArray abook = OpenMCU::Current().GetRegistrar()->GetAccountList();
  PStringStream msg;
  msg = "addressbook=Array(";
  for(PINDEX i = 0; i < abook.GetSize(); i++)
  {
    PString memberName = abook[i].Tokenise(",")[0];
    PString memberNameId = MCUURL(memberName).GetMemberNameId();
    PString abook_enable = abook[i].Tokenise(",")[1];
    PString remote_application = abook[i].Tokenise(",")[2];
    PString reg_state = abook[i].Tokenise(",")[3];
    PString reg_info = abook[i].Tokenise(",")[4];
    PString conn_state = abook[i].Tokenise(",")[5];
    PString conn_info = abook[i].Tokenise(",")[6];
    PString ping_state = abook[i].Tokenise(",")[7];
    PString ping_info = abook[i].Tokenise(",")[8];
    if(i>0) msg << ",";
    memberName.Replace("&","&amp;",TRUE,0);
    memberName.Replace("\"","&quot;",TRUE,0);
    msg << "Array("
        << "0"
        << ",\"" << memberNameId << "\""
        << ",\"" << memberName << "\""
        << ",\"" << abook_enable << "\""
        << ",\"" << remote_application << "\""
        << ",\"" << reg_state << "\""
        << ",\"" << reg_info << "\""
        << ",\"" << conn_state << "\""
        << ",\"" << conn_info << "\""
        << ",\"" << ping_state << "\""
        << ",\"" << ping_info << "\""
        << ")";
  }
  msg << ");p.abr()";
  OpenMCU::Current().HttpWriteCmdRoom(msg,number);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Conference::AddMemberToList(const PString & name, ConferenceMember *member)
{
  PWaitAndSignal m(memberListMutex);
  PWaitAndSignal m2(profileListMutex);

  // memberList
  if(member)
  {
    MemberList::iterator r = memberList.find(member->GetID());
    if(r != memberList.end())
      return;
    memberList.insert(MemberList::value_type(member->GetID(), member));
  }

  // memberProfileList
  PString nameID = MCUURL(name).GetMemberNameId();
  ConferenceProfile * profile = NULL;
  for(ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    if(r->second->GetMember())
      continue;
    if(r->second->GetNameID() == nameID)
    {
      profile = r->second;
      profile->SetMember(member);
      break;
    }
  }
  if(profile == NULL)
  {
    profile = new ConferenceProfile(this, name);
    profile->SetMember(member);
    profileList.insert(ProfileList::value_type((ConferenceMemberId)profile, profile));
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Conference::RemoveMemberFromList(const PString & name, ConferenceMember *member)
{
  PWaitAndSignal m(memberListMutex);
  PWaitAndSignal m2(profileListMutex);

  // memberList
  if(member)
    memberList.erase(member->GetID());

  // profileList
  for(ProfileList::iterator r = profileList.begin(); r != profileList.end(); ++r)
  {
    if(r->second->GetName() == name)
    {
      ConferenceProfile *profile = r->second;
      profile->Lock();
      if(member)
      {
        profile->SetMember(NULL);
        profile->Unlock();
      } else {
        profileList.erase(profile);
        delete profile;
      }
      break;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL Conference::AddMember(ConferenceMember * memberToAdd)
{

  PTRACE(3, "Conference\tAbout to add member " << memberToAdd->GetTitle() << " to conference " << guid);

  // see if the callback refuses the new member (always true)
  if (!BeforeMemberJoining(memberToAdd))
    return FALSE;

  memberToAdd->SetName();

  // lock the member lists
  PWaitAndSignal m(memberListMutex);
  PWaitAndSignal m2(profileListMutex);

  // check for duplicate name or very fast reconnect
  if(!memberToAdd->GetType() & MEMBER_TYPE_GSYSTEM)
  {
    // check for duplicate name or very fast reconnect
    PString memberName = memberToAdd->GetName();
    for(PINDEX i = 0; manager.FindMemberWithoutLock(this, memberToAdd->GetName()) != NULL; i++)
    {
      if(MCUConfig("Parameters").GetBoolean(RejectDuplicateNameKey, FALSE))
      {
        PString username = memberToAdd->GetName();
        username.Replace("&","&amp;",TRUE,0);
        username.Replace("\"","&quot;",TRUE,0);
        PStringStream msg;
        msg << username << " REJECTED - DUPLICATE NAME";
        OpenMCU::Current().HttpWriteEventRoom(msg, number);
        return FALSE;
      }
      memberToAdd->SetName(memberName+" ##"+PString(i+2));
    }
  }

#if MCU_VIDEO
  if (!UseSameVideoForAllMembers())
    memberToAdd->videoMixer = new MCUSimpleVideoMixer();
#endif

  PTRACE(3, "Conference\tAdding member: " << memberToAdd->GetName() << " to conference: " << guid);
  cout << "Conference Adding member: " << memberToAdd->GetName() << " to conference: " << guid << "\n";

  ConferenceMemberId mid = memberToAdd->GetID();
  MemberList::iterator r = memberList.find(mid);
  if(r != memberList.end())
    return FALSE;

#if MCU_VIDEO
  if(moderated==FALSE
#  if ENABLE_TEST_ROOMS
     || number=="testroom"
#  endif
    )
  {
    if(UseSameVideoForAllMembers() && memberToAdd->IsVisible())
    {
      videoMixerListMutex.Wait();
      if(!videoMixerList->GetMixer()->AddVideoSource(mid, *memberToAdd))
        memberToAdd->SetFreezeVideo(TRUE);
      videoMixerListMutex.Signal();
      PTRACE(3, "Conference\tUseSameVideoForAllMembers ");
    }
  }
  else
    memberToAdd->SetFreezeVideo(TRUE);
#endif

  // add to all lists
  AddMemberToList(memberToAdd->GetName(), memberToAdd);

  // for H.323
  int tid = terminalNumberMap.GetNumber(memberToAdd->GetID());
  memberToAdd->SetTerminalNumber(tid);

  // make sure each member has a connection created for the new member
  // make sure the new member has a connection created for each existing member
  PINDEX visibleMembers = 0;
  for(MemberList::iterator r = memberList.begin(); r != memberList.end(); r++)
  {
    ConferenceMember * conn = r->second;
    if(conn != memberToAdd)
    {
      conn->AddConnection(memberToAdd);
      memberToAdd->AddConnection(conn);
#if MCU_VIDEO
      if(moderated==FALSE
#  if ENABLE_TEST_ROOMS
         || number == "testroom"
#  endif
        )
      {
        if(!UseSameVideoForAllMembers())
        {
          if (conn->IsVisible())
          {
            memberToAdd->AddVideoSource(conn->GetID(), *conn);
          }
          if (memberToAdd->IsVisible())
          {
            conn->AddVideoSource(memberToAdd->GetID(), *memberToAdd);
          }
        }
#endif
      }
    }
    if(conn->IsVisible())
      ++visibleMembers;
  }

  // update the statistics
  if(memberToAdd->IsVisible())
  {
    maxMemberCount = PMAX(maxMemberCount, visibleMembers);
    // trigger H245 thread for join message
    //new NotifyH245Thread(*this, TRUE, memberToAdd);
  }

  // notify that member is joined
  memberToAdd->SetJoined(TRUE);

  // call the callback function
  OnMemberJoining(memberToAdd);

  // monitor
  if(memberToAdd->GetType() == MEMBER_TYPE_MCU && !mcuMonitorRunning)
  {
    manager.AddMonitorEvent(new ConferenceMCUCheckInfo(GetID(), 1000));
    mcuMonitorRunning = TRUE;
  }

  PStringStream msg;

  // add this member to the conference member name list
  if(!memberToAdd->GetType() & MEMBER_TYPE_GSYSTEM)
  {
    PullMemberOptionsFromTemplate(memberToAdd, confTpl);

    PString username=memberToAdd->GetName(); username.Replace("&","&amp;",TRUE,0); username.Replace("\"","&quot;",TRUE,0);
    msg="addmmbr(1";
    msg << "," << (long)memberToAdd->GetID()
        << ",\"" << username << "\""
        << "," << memberToAdd->muteMask
        << "," << memberToAdd->disableVAD
        << "," << memberToAdd->chosenVan
        << "," << memberToAdd->GetAudioLevel()
        << "," << memberToAdd->GetVideoMixerNumber()
        << ",\"" << memberToAdd->GetNameID() << "\""
        << "," << dec << (unsigned)memberToAdd->channelCheck
        << "," << memberToAdd->kManualGainDB
        << "," << memberToAdd->kOutputGainDB
        << ")";
    OpenMCU::Current().HttpWriteCmdRoom(msg,number);
  }

  msg = "<font color=green><b>+</b>";
  msg << memberToAdd->GetName() << "</font>"; OpenMCU::Current().HttpWriteEventRoom(msg,number);
  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL Conference::RemoveMember(ConferenceMember * memberToRemove)
{
  if(memberToRemove == NULL) return TRUE;

  // lock memberList
  PWaitAndSignal m(memberListMutex);
  PWaitAndSignal m2(profileListMutex);

  // lock conferenceMember
  memberToRemove->Lock();

  PString username = memberToRemove->GetName();

  if(!memberToRemove->IsJoined())
  {
    PTRACE(4, "Conference\tNo need to remove call " << username << " from conference " << guid);
    return (memberList.size() == 0);
  }

  ConferenceMemberId userid = memberToRemove->GetID();

  // add this member to the conference member name list with zero id

  PTRACE(3, "Conference\tRemoving call " << username << " from conference " << guid << " with size " << (PINDEX)memberList.size());
  cout << username << " leaving conference " << number << "(" << guid << ")" << endl;


  BOOL closeConference;
  {

    PStringStream msg; msg << "<font color=red><b>-</b>" << username << "</font>"; OpenMCU::Current().HttpWriteEventRoom(msg,number);
    username.Replace("&","&amp;",TRUE,0); username.Replace("\"","&quot;",TRUE,0);
    msg="remmmbr(0";
    msg << ","  << (long)userid
        << ",\"" << username << "\""
        << ","  << memberToRemove->muteMask
        << "," << memberToRemove->disableVAD
        << ","  << memberToRemove->chosenVan
        << ","  << memberToRemove->GetAudioLevel()
        << ",\"" << MCUURL(memberToRemove->GetName()).GetMemberNameId() << "\"";
// ???    if(s==memberNameList.end()) msg << ",1";
    msg << ")";
    OpenMCU::Current().HttpWriteCmdRoom(msg,number);

    // remove from all lists
    RemoveMemberFromList(memberToRemove->GetName(), memberToRemove);

    memberToRemove->RemoveAllConnections();

    MemberList::iterator r;
    // remove this member from the connection lists for all other members
    for (r = memberList.begin(); r != memberList.end(); r++) {
      ConferenceMember * conn = r->second;
      if(conn != NULL)
      if (conn != memberToRemove) {
        conn->RemoveConnection(userid);
#if MCU_VIDEO
        if (!UseSameVideoForAllMembers()) {
          if (memberToRemove->IsVisible())
            conn->RemoveVideoSource(userid, *memberToRemove);
          if (conn->IsVisible())
            memberToRemove->RemoveVideoSource(conn->GetID(), *conn);
        }
#endif
      }
    }

#if MCU_VIDEO
    if (moderated==FALSE
#  if ENABLE_TEST_ROOMS
    || number == "testroom"
#  endif
    )
    { if (UseSameVideoForAllMembers())
      if (memberToRemove->IsVisible())
      {
        PWaitAndSignal m(videoMixerListMutex);
        videoMixerList->GetMixer()->RemoveVideoSource(userid, *memberToRemove);
      }
    }
    else
    {
      PWaitAndSignal m(videoMixerListMutex);
      VideoMixerRecord * vmr=videoMixerList; while(vmr!=NULL)
      {
        vmr->GetMixer()->MyRemoveVideoSourceById(userid,FALSE);
        vmr=vmr->next;
      }
    }
#endif

    // trigger H245 thread for leave message
//    if (memberToRemove->IsVisible())
//      new NotifyH245Thread(*this, FALSE, memberToRemove);

    terminalNumberMap.RemoveNumber(memberToRemove->GetTerminalNumber());


    // return TRUE if conference is empty 
    closeConference = GetVisibleMemberCount() == 0;
  }

  // notify that member is not joined anymore
  memberToRemove->SetJoined(FALSE);

  // call the callback function
  OnMemberLeaving(memberToRemove);

  return closeConference;
}


void Conference::ReadMemberAudio(ConferenceMember * member, void * buffer, PINDEX amount, unsigned sampleRate, unsigned channels)
{
  // get number of channels to mix
  // lock in ConferenceMember::ReadAudio
  ConferenceMember::ConnectionListType & connectionList = member->GetConnectionList();
  for (ConferenceMember::ConnectionListType::iterator r=connectionList.begin(), e=connectionList.end(); r!=e; ++r) 
  {
    if (r->second != NULL)
    {
      PWaitAndSignal m(((ConferenceConnection*)r->second)->audioBufferMutex);
      BOOL skip=moderated&&muteUnvisible;
      if(skip)
      {
        PWaitAndSignal m(videoMixerListMutex);
        VideoMixerRecord * vmr = videoMixerList;
        while(vmr != NULL) if(vmr->GetMixer()->GetPositionStatus(r->first)>=0)
        { skip=FALSE; break; }
        else vmr=vmr->next;
      }
      if(!skip) // default behaviour
      {
        r->second->ReadAndMixAudio((BYTE *)buffer, amount, (PINDEX)connectionList.size(), 0, sampleRate, channels);
        if(member) if(member->kOutputGainDB)
        {
          float k = member->kOutputGain;
          unsigned i = (amount >> 1);
          char * pos = (char*)buffer;
          while(i)
          {
            int c = (int)(k*(*(short*)pos));
            if(c>32766) *(short*)pos = 32767;
            else if(c<-32767) *(short*)pos = -32768;
            else *(short*)pos = (short)c;
            pos+=2;
            i--;
          }
        }
      }
    }
  }
}

// tint - time interval since last call in msec
void Conference::WriteMemberAudioLevel(ConferenceMember * member, unsigned audioLevel, int tint)
{
  member->audioLevelIndicator|=audioLevel;
  if(!((++member->audioCounter)&31)){
    if (member->audioLevelIndicator < 64) member->audioLevelIndicator = 0;
    if((member->previousAudioLevel != member->audioLevelIndicator)||((member->audioLevelIndicator!=0) && ((member->audioCounter&255)==0))){
      PStringStream msg; msg << "audio(" << (long)member->GetID() << "," << member->audioLevelIndicator << ")";
      OpenMCU::Current().HttpWriteCmdRoom(msg,number);
      member->previousAudioLevel=member->audioLevelIndicator;
      member->audioLevelIndicator=audioLevel;
    }
  }
#if MCU_VIDEO
  if (UseSameVideoForAllMembers())
  {
    PWaitAndSignal m(videoMixerListMutex);
    if (videoMixerList != NULL)
    {
      if(audioLevel > VAlevel) member->vad+=tint;
      else member->vad=0;
      VideoMixerRecord * vmr=videoMixerList;
      while(vmr!=NULL)
      {
        MCUVideoMixer * videoMixer = vmr->GetMixer();
        int status = videoMixer->GetPositionStatus(member->GetID());
        if(audioLevel > VAlevel)
        {
//          cout << "audioLevel " << audioLevel << "\n";
          if(member->vad >= VAdelay) // voice-on trigger delay
          {
//            cout << "VAD=" << member->vad << " status=" << status << "\n";
            if(status > 0) videoMixer->SetPositionStatus(member->GetID(),0);
            else if(status == 0 && member->disableVAD == FALSE)
            {
              if(member->vad-VAdelay>500) // execute every 500 ms of voice activity
                videoMixer->SetVAD2Position(member);
            }
            else if(status == -1 && member->disableVAD == FALSE) //find new vad position for active member
            {
              ConferenceMemberId id = videoMixer->SetVADPosition(member,member->chosenVan,VAtimeout);
              if(id!=NULL)
              { FreezeVideo(id); member->SetFreezeVideo(FALSE); }
            }
          }
        }
        else
        {
          if(status >= 0) // increase silence counter
            videoMixer->SetPositionStatus(member->GetID(),status+tint);
        }
        if(audioLevel > VAlevel) if(status==0 && member->disableVAD==FALSE) if(member->vad-VAdelay>500) member->vad=VAdelay;
        vmr=vmr->next;
      }
    }
  }
#endif // MCU_VIDEO
}


#if MCU_VIDEO

void Conference::ReadMemberVideo(ConferenceMember * member, void * buffer, int width, int height, PINDEX & amount)
{
  PWaitAndSignal m(videoMixerListMutex);
  if (videoMixerList == NULL)
    return;

// PTRACE(3, "Conference\tReadMemberVideo call 1" << width << "x" << height);
  unsigned mixerNumber; if(member==NULL) mixerNumber=0; else mixerNumber=member->GetVideoMixerNumber();
  MCUVideoMixer * mixer = VMLFind(mixerNumber);

  if(mixer==NULL)
  { if(mixerNumber != 0) mixer = VMLFind(0);
    if(mixer==NULL)
    { PTRACE(3,"Conference\tCould not get video");
      return;
    } else { PTRACE(6,"Conference\tCould not get video mixer " << mixerNumber << ", reading 0 instead"); }
  }

//  if (mixer!=NULL) {
    mixer->ReadFrame(*member, buffer, width, height, amount);
//    return;
//  }

/* commented by kay27 not really understanding what he is doing, 04.09.2013
  // find the other member and copy it's video
  PWaitAndSignal m(memberListMutex);
  MemberList::iterator r;
  for (r = memberList.begin(); r != memberList.end(); r++) {
    if ((r->second != member) && r->second->IsVisible()) {
      void * frameStore = r->second->OnExternalReadVideo(member->GetID(), width, height, amount);  
      if (frameStore != NULL) {
        memcpy(buffer, frameStore, amount);
        r->second->UnlockExternalVideo();
      }
    }
  }
*/
  
}

BOOL Conference::WriteMemberVideo(ConferenceMember * member, const void * buffer, int width, int height, PINDEX amount)
{
  if (UseSameVideoForAllMembers())
  {
    PWaitAndSignal m(videoMixerListMutex);
    if (videoMixerList != NULL) {
      VideoMixerRecord * vmr = videoMixerList; BOOL writeResult=FALSE;
      while(vmr!=NULL)
      { writeResult |= vmr->GetMixer()->WriteFrame(member->GetID(), buffer, width, height, amount);
        vmr=vmr->next;
      }
      return writeResult;
    }
  }
  else
  {
    PWaitAndSignal m(memberListMutex);
    MemberList::iterator r;
    for (r = memberList.begin(); r != memberList.end(); ++r)
      r->second->OnExternalSendVideo(member->GetID(), buffer, width, height, amount);
  }
  return TRUE;
}

#endif

BOOL Conference::BeforeMemberJoining(ConferenceMember * member)
{ 
  return manager.BeforeMemberJoining(this, member); 
}

void Conference::OnMemberJoining(ConferenceMember * member)
{ 
  manager.OnMemberJoining(this, member); 
}

void Conference::OnMemberLeaving(ConferenceMember * member)
{ 
  manager.OnMemberLeaving(this, member); 
}

void Conference::FreezeVideo(ConferenceMemberId id)
{ 
  int i;
  PWaitAndSignal m(memberListMutex);
  MemberList::iterator r,s;
  if(id!=NULL)
  {
    r = memberList.find(id); if(r == memberList.end()) return;
    PWaitAndSignal m(videoMixerListMutex);
    VideoMixerRecord * vmr = videoMixerList;
    while(vmr!=NULL)
    { i=vmr->GetMixer()->GetPositionStatus(id);
      if(i>=0) {
        r->second->SetFreezeVideo(FALSE);
        return;
      }
      vmr=vmr->next;
    }
    if(!UseSameVideoForAllMembers())for(s=memberList.begin();s!=memberList.end();++s) if(s->second->videoMixer!=NULL) if(s->second->videoMixer->GetPositionStatus(id)>=0) {r->second->SetFreezeVideo(FALSE); return;}
    r->second->SetFreezeVideo(TRUE);
    return;
  }
  for (r = memberList.begin(); r != memberList.end(); r++)
  {
    ConferenceMemberId mid=r->second->GetID();
    PWaitAndSignal m(videoMixerListMutex);
    VideoMixerRecord * vmr = videoMixerList;
    while(vmr!=NULL)
    {
      i=vmr->GetMixer()->GetPositionStatus(mid);
      if(i>=0)
      {
        r->second->SetFreezeVideo(FALSE);
        break;
      }
      vmr=vmr->next;
    }
    if(vmr==NULL)
    { if(!UseSameVideoForAllMembers())
      { for(s=memberList.begin();s!=memberList.end();++s)
        { if(s->second->videoMixer!=NULL) if(s->second->videoMixer->GetPositionStatus(mid)>=0)
          { r->second->SetFreezeVideo(FALSE);
            break;
          }
        }
        if(s==memberList.end()) r->second->SetFreezeVideo(TRUE);
      }
      else r->second->SetFreezeVideo(TRUE);
    }
  }
}

BOOL Conference::PutChosenVan()
{
  BOOL put=FALSE;
  int i;
  PWaitAndSignal m(memberListMutex);
  MemberList::iterator r;
  for (r = memberList.begin(); r != memberList.end(); ++r)
  {
    if(r->second->chosenVan)
    {
      PWaitAndSignal m(videoMixerListMutex);
      VideoMixerRecord * vmr = videoMixerList;
      while(vmr!=NULL){
        i=vmr->GetMixer()->GetPositionStatus(r->second->GetID());
        if(i < 0) put |= (NULL != vmr->GetMixer()->SetVADPosition(r->second,r->second->chosenVan,VAtimeout));
        vmr = vmr->next;
      }
    }
  }
  return put;
}

void Conference::HandleFeatureAccessCode(ConferenceMember & member, PString fac){
  PTRACE(3,"Conference\tHandling feature access code " << fac << " from " << member.GetName());
  PStringArray s = fac.Tokenise("*");
  if(s[0]=="1")
  {
    int posTo=0;
    if(s.GetSize()>1) posTo=s[1].AsInteger();
    PTRACE(4,"Conference\t" << "*1*" << posTo << "#: jump into video position " << posTo);
    if(videoMixerCount==0) return;
    ConferenceMemberId id=member.GetID();
    if(id==NULL) return;

    {
      PWaitAndSignal m(videoMixerListMutex);
      if(videoMixerList==NULL) return;
      if(videoMixerList->GetMixer()==NULL) return;
      int pos=videoMixerList->GetMixer()->GetPositionNum(id);
      if(pos==posTo) return;
      videoMixerList->GetMixer()->InsertVideoSource(&member,posTo);
    }
    FreezeVideo(NULL);

    OpenMCU::Current().HttpWriteCmdRoom(OpenMCU::Current().GetEndpoint().GetConferenceOptsJavascript(*this),number);
    OpenMCU::Current().HttpWriteCmdRoom("build_page()",number);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceProfile::ConferenceProfile(Conference * _conference, PString _name)
{
  conference = _conference;
  SetName(_name);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferenceProfile::SetName(PString _name)
{
  PWaitAndSignal m(mutex);
  name = _name;
  nameID = MCUURL(name).GetMemberNameId();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ConferenceProfile::SetMember(ConferenceMember * _member)
{
  PWaitAndSignal m(mutex);
  member = _member;
  if(member)
    SetName(member->GetName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ConferenceMember::ConferenceMember(Conference * _conference, ConferenceMemberId _id)
  : conference(_conference), id(_id)
{
  memberType = MEMBER_TYPE_NONE;
  channelCheck=0;
  audioLevel = 0;
  audioCounter = 0; previousAudioLevel = 65535; audioLevelIndicator = 0;
  currVolCoef = 1.0;
  kManualGain = 1.0; kManualGainDB = 0;
  kOutputGain = 1.0; kOutputGainDB = 0;
  terminalNumber = -1;
  memberIsJoined = FALSE;

#if MCU_VIDEO
  videoMixer = NULL;
//  fsConverter = PColourConverter::Create("YUV420P", "YUV420P", CIF4_WIDTH, CIF4_HEIGHT);
//  MCUVideoMixer::FillCIF4YUVFrame(memberFrameStores.GetFrameStore(CIF4_WIDTH, CIF4_HEIGHT).data.GetPointer(), 0, 0, 0);
  totalVideoFramesReceived = 0;
  firstFrameReceiveTime = -1;
  totalVideoFramesSent = 0;
  firstFrameSendTime = -1;
  rxFrameWidth = 0; rxFrameHeight = 0;
  vad = 0;
  autoDial = FALSE;
  muteMask = 0;
  disableVAD = FALSE;
  chosenVan = 0;
  videoMixerNumber = 0;
#endif
}

ConferenceMember::~ConferenceMember()
{
  muteMask|=15;
#if MCU_VIDEO
  if(videoMixer)
  {
    videoMixer->FullLock();
    delete videoMixer;
  }
#endif
}   

void ConferenceMember::ChannelBrowserStateUpdate(BYTE bitMask, BOOL bitState)
{
  if(bitState)
  {
    channelCheck|=bitMask;
  }
  else
  {
    channelCheck&=~bitMask;
  }

  PStringStream msg;
  msg << "rtp_state(" << dec << (long)id << ", " << (unsigned)channelCheck << ")";
  PTRACE(1,"channelCheck change: " << (bitState?"+":"-") << (unsigned)bitMask << ". Result: " << (unsigned)channelCheck << ", conference=" << conference << ", msg=" << msg);

  if(!conference)
  {
    OpenMCU::Current().HttpWriteCmd(msg);
    return;
  }

  OpenMCU::Current().HttpWriteCmdRoom(msg,conference->GetNumber());
}

void ConferenceMember::AddConnection(ConferenceMember * memberToAdd)
{
  ConferenceMemberId newID = memberToAdd->GetID();
  PTRACE(3, "Conference\tAdding " << newID << " to connection " << id);
  if (lock.Wait(TRUE)) {
    ConferenceConnection * conn = memberToAdd->CreateConnection();
    memberList.insert(MemberListType::value_type(newID, memberToAdd));
    connectionList.insert(ConnectionListType::value_type(newID, conn));
    lock.Signal(TRUE);
  }
}

void ConferenceMember::RemoveConnection(ConferenceMemberId idToDelete)
{
  PTRACE(3, "Conference\tRemoving member " << idToDelete << " from connection " << id);
  if (lock.Wait(TRUE)) {
    memberList.erase(idToDelete);
    connectionList.erase(idToDelete);
    lock.Signal(TRUE);
  }
}

void ConferenceMember::RemoveAllConnections()
{
  PTRACE(3, "Conference\tRemoving all members from connection " << id);
  if (lock.Wait(TRUE)) {
    memberList.clear();
    connectionList.clear();
    for(BufferListType::iterator t0=bufferList.begin(); t0!=bufferList.end(); ++t0)
    if(t0->second != NULL) {
#if USE_SWRESAMPLE
      if(t0->second->swrc != NULL) swr_free(&(t0->second->swrc));
      t0->second->swrc = NULL;
#elif USE_AVRESAMPLE
      if(t0->second->swrc != NULL) avresample_free(&(t0->second->swrc));
      t0->second->swrc = NULL;
#elif USE_LIBSAMPLERATE
      if(t0->second->swrc != NULL) src_delete(t0->second->swrc);
      t0->second->swrc = NULL;
#endif
      delete t0->second; t0->second=NULL;
    }
    PTRACE(3,"Conference\tResampling buffers removed from connection " << id);
    lock.Signal(TRUE);
  }
}

void AutoGainControl(const short * pcm, unsigned samplesPerFrame, unsigned codecChannels, unsigned sampleRate, unsigned level, float* currVolCoef, unsigned* signalLevel, float kManual)
{
  unsigned samplesCount = samplesPerFrame*codecChannels;
  if(!samplesCount) return;

  const short * end = pcm + samplesCount;
  short *buf = (short*)pcm;
  int c_max_vol = 0, c_avg_vol = 0;
  while (pcm != end) 
  {
    if (*pcm < 0) 
    { if(-*pcm > c_max_vol) c_max_vol = -*pcm; c_avg_vol -= *pcm++; }
    else 
    { if( *pcm > c_max_vol) c_max_vol =  *pcm; c_avg_vol += *pcm++; }
  }
  c_avg_vol /= samplesPerFrame;

  if(!level)
  {
    *signalLevel = c_avg_vol;
    return;
  }

  float   max_vol = (float)23170.0 * kManual;
  float   overload = 32768 * kManual;
  float   inc_vol = (float)0.05*(float)8000.0/sampleRate;
  float & cvc = *currVolCoef;
  float   vc0= cvc;
  
  unsigned wLevel;
  if(kManual>10) wLevel = (unsigned)(level*10/kManual); else wLevel=level;

  if((unsigned)c_avg_vol > wLevel)
  {
    if(c_max_vol*cvc >= overload) // есть перегрузка
      cvc = overload / c_max_vol;
    else
    if(c_max_vol*cvc < max_vol) // нужно увеличить усиление
      cvc += inc_vol;
  }
  else // не должен срабатывать, но временно грубая защита от перегрузки:
  {
    if(c_max_vol*cvc >= overload) // есть перегрузка
      cvc = (float)overload / c_max_vol;
  }
  *signalLevel = (unsigned)((float)c_avg_vol*cvc);
  PTRACE(9,"AGC\tavg" << c_avg_vol << " max" << c_max_vol << " vc" << vc0 << ">" << cvc);

  float delta0=(cvc-vc0)/samplesCount;

  for(unsigned i=0; i<samplesCount; i++) 
  {
    int v = buf[i];
    v=(int)(v*vc0);
    if(v > 32767) buf[i]=32767;
    else if(v < -32768) buf[i]=-32768;
    else buf[i] = (short)v;
    vc0+=delta0;
  }
}

ResamplerBufferType * ConferenceMember::CreateResamplerBuffer(unsigned bufferKey, unsigned sampleRate, unsigned targetSampleRate, unsigned channels, unsigned targetChannels)
{
  ResamplerBufferType * newResamplerBuffer = new ResamplerBufferType();
  newResamplerBuffer->used = FALSE;
#if USE_SWRESAMPLE
  newResamplerBuffer->swrc = NULL;
  newResamplerBuffer->swrc = swr_alloc_set_opts(NULL,
  MCU_AV_CH_Layout_Selector[targetChannels], AV_SAMPLE_FMT_S16, targetSampleRate,
  MCU_AV_CH_Layout_Selector[channels      ], AV_SAMPLE_FMT_S16, sampleRate,       0, NULL);
  swr_init(newResamplerBuffer->swrc);
#elif USE_AVRESAMPLE
  newResamplerBuffer->swrc = NULL;
  newResamplerBuffer->swrc = avresample_alloc_context();
  av_opt_set_int(newResamplerBuffer->swrc, "out_channel_layout", MCU_AV_CH_Layout_Selector[targetChannels], 0);
  av_opt_set_int(newResamplerBuffer->swrc, "out_sample_fmt",     AV_SAMPLE_FMT_S16, 0);
  av_opt_set_int(newResamplerBuffer->swrc, "out_sample_rate",    targetSampleRate, 0);
  av_opt_set_int(newResamplerBuffer->swrc, "in_channel_layout",  MCU_AV_CH_Layout_Selector[channels], 0);
  av_opt_set_int(newResamplerBuffer->swrc, "in_sample_fmt",      AV_SAMPLE_FMT_S16,0);
  av_opt_set_int(newResamplerBuffer->swrc, "in_sample_rate",     sampleRate, 0);
  avresample_open(newResamplerBuffer->swrc);
#elif USE_LIBSAMPLERATE
  newResamplerBuffer->swrc = NULL;
  newResamplerBuffer->swrc = src_new(SRC_LINEAR, 1, NULL);
#endif
  bufferList.insert(BufferListType::value_type(bufferKey, newResamplerBuffer));
  return newResamplerBuffer;
}

void ConferenceMember::WriteAudio(const void * buffer, PINDEX amount, unsigned sampleRate, unsigned channels)
{
  if(!(channelCheck&1)) ChannelBrowserStateUpdate(1,TRUE);
  if(muteMask&1) return;

  // calculate average signal level for this member
  unsigned signalLevel=0;
  AutoGainControl((short*) buffer, amount/channels/2, channels, sampleRate, 2000, &currVolCoef, &signalLevel, kManualGain);
  audioLevel = ((signalLevel * 2) + audioLevel) / 3;

  if(lock.Wait())
  {
    if(conference != NULL)
      conference->WriteMemberAudioLevel(this, audioLevel, amount/32);

    // reset buffer usage flag
    for(BufferListType::iterator t = bufferList.begin(); t != bufferList.end(); ++t)
      if(t->second != NULL)
        t->second->used = FALSE;

    for(MemberListType::iterator r = memberList.begin(); r != memberList.end(); ++r)
    {
      if(r->second == NULL) // member in the list != NULL
        continue;

      ConnectionListType::iterator s = r->second->connectionList.find(id);
      if(s == r->second->connectionList.end())
        continue; // seems this is really needs for same-time connections
      if(s->second == NULL)
        continue; // one more paranoidal check just in case

      if(s->second->outgoingSampleRate == sampleRate && s->second->outgoingCodecChannels == channels)
        s->second->Write((BYTE *)buffer, amount); // equal rates
      else // resampler needs here
      {
        unsigned targetSampleRate = s->second->outgoingSampleRate;
        unsigned targetChannels = s->second->outgoingCodecChannels;
        unsigned bufferKey = (targetChannels<<24)|targetSampleRate;
        PINDEX newAmount = amount * targetChannels * targetSampleRate / sampleRate / channels;
        newAmount -= (newAmount % (channels<<1));

        ResamplerBufferType *resBuffer = NULL;
        BufferListType::iterator t = bufferList.find(bufferKey);
        if(t != bufferList.end())
          resBuffer = t->second;
        else
          resBuffer = CreateResamplerBuffer(bufferKey, sampleRate, targetSampleRate, channels, targetChannels);
        if(resBuffer == NULL)
          continue;

        if(!resBuffer->used)
        {
          if(resBuffer->data.GetSize() < newAmount)
            resBuffer->data.SetSize(newAmount + 16);
          DoResample((BYTE *)buffer, amount, sampleRate, channels, resBuffer, newAmount, targetSampleRate, targetChannels);
          resBuffer->used = TRUE;
        }
        s->second->Write((BYTE *)(resBuffer->data.GetPointer()), newAmount);
      }
    }

    // delete unused buffers
    BufferListType::iterator t = bufferList.begin();
    while(t != bufferList.end())
    {
      if(t->second) if(!t->second->used)
      {
#if USE_SWRESAMPLE
        if(t->second->swrc) swr_free(&(t->second->swrc));
        t->second->swrc = NULL;
#elif USE_AVRESAMPLE
        if(t->second->swrc) avresample_free(&(t->second->swrc));
        t->second->swrc = NULL;
#elif USE_LIBSAMPLERATE
        if(t->second->swrc) src_delete(t->second->swrc);
        t->second->swrc = NULL;
#endif
        delete t->second;
        t->second = NULL;
        bufferList.erase(t);
        t = bufferList.begin(); // needed at least for msvc++ 2010
                                // (it fixes access violation when thread after last iteration strangely failed still here)
        continue;
      }
      t++;
    }

    lock.Signal();
  }
}

void ConferenceMember::DoResample(BYTE * src, PINDEX srcBytes, unsigned srcRate, unsigned srcChannels, ResamplerBufferType *resBuffer, PINDEX dstBytes, unsigned dstRate, unsigned dstChannels)
{
#if USE_SWRESAMPLE
  void * to = resBuffer->data.GetPointer();
  void * from = (void*)src;
  swr_convert(resBuffer->swrc,
    (uint8_t **)&to,
       (((int)dstBytes)>>1)/dstChannels,
    (const uint8_t **)&from,
       (((int)srcBytes)>>1)/srcChannels
  );
#elif USE_AVRESAMPLE
  void * to = resBuffer->data.GetPointer();
  void * from = (void*)src;

  int out_samples = (((int)dstBytes)>>1)/dstChannels;
  int out_linesize = (int)dstBytes;
  int in_samples = (((int)srcBytes)>>1)/srcChannels;
  int in_linesize = (int)srcBytes;

  avresample_convert(resBuffer->swrc, (uint8_t **)&to, out_linesize, out_samples,
                                      (uint8_t **)&from, in_linesize, in_samples);
#elif USE_LIBSAMPLERATE
  SRC_DATA src_data;
  long out_samples = (int)dstBytes/(dstChannels*sizeof(short));
  long in_samples = (int)srcBytes/(srcChannels*sizeof(short));
  float data_out[out_samples*sizeof(float)];
  float data_in[in_samples*sizeof(float)];
  src_short_to_float_array((const short *)src, data_in, in_samples);

  src_data.data_in = data_in;
  src_data.input_frames = in_samples;
  src_data.data_out = data_out;
  src_data.output_frames = out_samples;
  src_data.src_ratio = (double)out_samples/(double)in_samples;

  int err = src_process(resBuffer->swrc, &src_data);
  if (err)
  {
    PTRACE(1, "libsamplerate error: " << src_strerror(err));
    return;
  }
  src_float_to_short_array(data_out, (short *)resBuffer->data.GetPointer(), src_data.output_frames_gen);
  //PTRACE(1, "libsamplerate: " << src_data.input_frames << " " << src_data.output_frames << " " << src_data.input_frames_used << " " << src_data.output_frames_gen);
#else
  if(srcChannels == dstChannels && srcChannels == 1)
  {
    for(PINDEX i=0;i<(dstBytes>>1);i++) ((short*)(resBuffer->data.GetPointer()))[i] = ((short*)src)[i*srcRate/dstRate];
    return;
  }
  if(srcChannels == dstChannels)
  {
    for(unsigned i=0;i<((dstBytes>>1)/dstChannels);i++)
    {
      unsigned ofs=(i*srcRate/dstRate)*srcChannels;
      for(unsigned j=0;j<srcChannels;j++) ((short*)(resBuffer->data.GetPointer()))[i*srcChannels+j] = ((short*)src)[ofs+j];
    }
    return;
  }
  for(unsigned i=0;i<(dstBytes>>1)/dstChannels;i++)
  {
    unsigned ofs=(i*srcRate/dstRate)*srcChannels, srcChan=0;
    for(unsigned j=0;j<dstChannels;j++)
    {
      ((short*)(resBuffer->data.GetPointer()))[i*dstChannels+j] = ((short*)src)[ofs+srcChan];
      srcChan++; if(srcChan>=srcChannels) srcChan=0;
    }
  }
#endif
}

void ConferenceMember::OnExternalSendAudio(ConferenceMemberId source, const void * buffer, PINDEX amount, unsigned sampleRate)
{
  if (lock.Wait()) {
    ConnectionListType::iterator r = connectionList.find(source);
    if (r != connectionList.end())
      if (r->second != NULL)
        r->second->Write((BYTE *)buffer, amount);
    lock.Signal();
  }
}

void ConferenceMember::ReadAudio(void * buffer, PINDEX amount, unsigned sampleRate, unsigned channels)
{
  if(!(channelCheck&2)) ChannelBrowserStateUpdate(2,TRUE);

  // First, set the buffer to empty.
  memset(buffer, 0, amount);

  if(muteMask&2) return;

  if (lock.Wait()) {
    if (conference != NULL)
      conference->ReadMemberAudio(this, buffer, amount, sampleRate, channels);
    lock.Signal();
  }
}

#if MCU_VIDEO

// called whenever the connection needs a frame of video to send
void ConferenceMember::ReadVideo(void * buffer, int width, int height, PINDEX & amount)
{
  if(!(channelCheck&8)) ChannelBrowserStateUpdate(8,TRUE);
//  if(muteMask&8) { memset(buffer,0,amount); return; }
  if(muteMask&8) return;
  ++totalVideoFramesSent;
  if (!firstFrameSendTime.IsValid())
    firstFrameSendTime = PTime();
  if (lock.Wait()) {
    if (conference != NULL) {
      if (conference->UseSameVideoForAllMembers())
        conference->ReadMemberVideo(this, buffer, width, height, amount);
      else if (videoMixer != NULL)
        videoMixer->ReadFrame(*this, buffer, width, height, amount);
    }
    lock.Signal();
  }
}

// called whenever the connection receives a frame of video
void ConferenceMember::WriteVideo(const void * buffer, int width, int height, PINDEX amount)
{
  if(!(channelCheck&4)) ChannelBrowserStateUpdate(4,TRUE);
  if(muteMask&4) return;
  ++totalVideoFramesReceived;
  rxFrameWidth=width; rxFrameHeight=height;
  if (!firstFrameReceiveTime.IsValid())
    firstFrameReceiveTime = PTime();

  if (lock.Wait()) {
    if (conference != NULL) {
      if (!conference->WriteMemberVideo(this, buffer, width, height, amount)) {
        PWaitAndSignal m(memberFrameStoreMutex);
        VideoFrameStoreList::FrameStore & fs = memberFrameStores.GetFrameStore(width, height);
        memcpy(fs.data.GetPointer(), buffer, amount);
        memberFrameStores.InvalidateExcept(width, height);
      }
    }
    lock.Signal();
  }
}

void ConferenceMember::OnExternalSendVideo(ConferenceMemberId id, const void * buffer, int width, int height, PINDEX amount)
{
//  if (lock.Wait()) {
    videoMixer->WriteFrame(id, buffer, width, height, amount);
//    lock.Signal();
//  }
}

void * ConferenceMember::OnExternalReadVideo(ConferenceMemberId id, int width, int height, PINDEX & bytesReturned)
{
  if (!lock.Wait())
    return NULL;

  memberFrameStoreMutex.Wait();

  BOOL found;
  VideoFrameStoreList::FrameStore & nearestFs = memberFrameStores.GetNearestFrameStore(width, height, found);

  // if no valid framestores, nothing we can do
/*  if (!found) {
    memberFrameStoreMutex.Signal();
    lock.Signal();
    return NULL;
  }
*/
  // if the valid framestore is a perfect match, return it
  if(found)
  if ((nearestFs.width == width) && (nearestFs.height == height))
    return nearestFs.data.GetPointer();

  // create a new destinationf framestore
  VideoFrameStoreList::FrameStore & destFs = memberFrameStores.GetFrameStore(width, height);

  if(found)
  MCUVideoMixer::ResizeYUV420P(nearestFs.data.GetPointer(), destFs.data.GetPointer(), nearestFs.width, nearestFs.height, width, height);
  else
//  OpenMCU::Current().GetPreMediaFrame(destFs.data.GetPointer(), width, height, bytesReturned);
  MCUVideoMixer::FillYUVFrame(destFs.data.GetPointer(), 0, 0, 0, width, height);
  destFs.valid = TRUE;

  return destFs.data.GetPointer();
}

void ConferenceMember::UnlockExternalVideo()
{ 
  memberFrameStoreMutex.Signal(); 
  lock.Signal();
}

BOOL ConferenceMember::AddVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PAssert(videoMixer != NULL, "attempt to add video source to NULL video mixer");
  return videoMixer->AddVideoSource(id, mbr);
}

void ConferenceMember::RemoveVideoSource(ConferenceMemberId id, ConferenceMember & mbr)
{
  PAssert(videoMixer != NULL, "attempt to remove video source from NULL video mixer");
  videoMixer->RemoveVideoSource(id, mbr);
}

BOOL ConferenceMember::OnOutgoingVideo(void * buffer, int width, int height, PINDEX & amount)
{
  return FALSE;
}

BOOL ConferenceMember::OnIncomingVideo(const void * buffer, int width, int height, PINDEX amount)
{
  return FALSE;
}

#endif

PString ConferenceMember::GetMonitorInfo(const PString & /*hdr*/)
{ 
  return PString::Empty(); 
}

///////////////////////////////////////////////////////////////////////////

ConferenceConnection::ConferenceConnection(ConferenceMemberId _id)
//  : id(_id), bufferSize(PCM_BUFFER_SIZE)
  : id(_id)
{
  outgoingSampleRate = 8000;
  outgoingCodecChannels = 1;
  PCM_BUFFER_SIZE_CALC(outgoingSampleRate,outgoingCodecChannels);
  bufferStart = bufferLen = 0;
  hasUnderflow = TRUE; // we would like to accumulate some data before we'll hear it :)
}

ConferenceConnection::~ConferenceConnection()
{
  PWaitAndSignal m(audioBufferMutex);
  bufferLen=-0x800000;
//  delete[] buffer;
}

void ConferenceConnection::Write(const BYTE * data, PINDEX amount)
{
  if (amount == 0) return;

  PWaitAndSignal mutex(audioBufferMutex);
  
  // if there is not enough room for the new data, make room
  PINDEX newLen = bufferLen + amount;
  if (newLen > bufferSize) {
    PINDEX toRemove = newLen - bufferSize;
    bufferStart = (bufferStart + toRemove) % bufferSize;
    bufferLen -= toRemove;
  }

  // copy data to the end of the new data, up to the end of the buffer
  PINDEX copyStart = (bufferStart + bufferLen) % bufferSize;
  if ((copyStart + amount) > bufferSize) {
    PINDEX toCopy = bufferSize - copyStart;
    memcpy((BYTE *)((unsigned long)buffer.GetPointer() + copyStart), data, toCopy);
    copyStart = 0;
    data      += toCopy;
    amount    -= toCopy;
    bufferLen += toCopy;
  }

  // copy the rest of the data
  if (amount > 0) {
    memcpy((BYTE *)((unsigned long)buffer.GetPointer() + copyStart), data, amount);
    bufferLen   += amount;
  }
}


void ConferenceConnection::ReadAudio(BYTE * data, PINDEX amount)
{
  if (amount == 0) return;

  PWaitAndSignal mutex(audioBufferMutex);
  
  if (bufferLen == 0) {
    memset(data, 0, amount); // nothing in the buffer. return silence
    return;
  }

  // fill output data block with silence if audiobuffer is
  // almost empty.
  if (amount > bufferLen) 
    memset(data + bufferLen, 0, amount - bufferLen);

  // only copy up to the amount of data remaining
  PINDEX copyLeft = PMIN(amount, bufferLen);

  // if buffer is wrapping, get first part
  if ((bufferStart + copyLeft) > bufferSize) {
    PINDEX toCopy = bufferSize - bufferStart;

    memcpy(data, (BYTE *)((unsigned long)buffer.GetPointer() + bufferStart), toCopy);

    data        += toCopy;
    bufferLen   -= toCopy;
    copyLeft    -= toCopy;
    bufferStart = 0;
  }

  // get the remainder of the buffer
  if (copyLeft > 0) {

    memcpy(data, (BYTE *)((unsigned long)buffer.GetPointer() + bufferStart), copyLeft);

    bufferLen -= copyLeft;
    bufferStart = (bufferStart + copyLeft) % bufferSize;
  }
}

void ConferenceConnection::ReadAndMixAudio(BYTE * data, PINDEX amount, PINDEX channels, unsigned short echoLevel, unsigned sampleRate, unsigned codecChannels)
{ 
  PWaitAndSignal mutex(audioBufferMutex);
  
  if(outgoingSampleRate != sampleRate || outgoingCodecChannels != codecChannels)
  { // sample rate or stereo mode changed
    PTRACE(5,"Mixer\tChanged outgoing sample rate: " << outgoingSampleRate << "->" << sampleRate << ", codec channels: " << outgoingCodecChannels << "->" << codecChannels);
    PCM_BUFFER_SIZE_CALC(sampleRate,codecChannels);
    memset(buffer.GetPointer(),0,bufferSize);
    bufferStart=0; bufferLen=0;
    outgoingSampleRate=sampleRate;
    outgoingCodecChannels=codecChannels;
  }

  if (amount <= 0) { PTRACE(6, "Mixer\tConn " << id << " requested 0 bytes"); return;}

  if(bufferLen < amount)
  {
    if(!hasUnderflow) { PTRACE(6,"Mixer\tConn " << id << " audio buffer underflow: needs " << amount << ", has " << bufferLen << " bytes - silenced"); }
    hasUnderflow = TRUE;
    return;
  }

  if(hasUnderflow)
  {
    if (bufferSize/bufferLen > 2) // waiting at least one half of buffer to continue mixing
    {
      PTRACE(6,"Mixer\tConn " << id << " accumulating audio buffer after underflow, " << (bufferLen*100/bufferSize) << "%, still silencing");
      return;
    }
    else hasUnderflow = FALSE;
  }

  // only mix up to the amount of data remaining
  PINDEX copyLeft = PMIN(amount, bufferLen);

  // if buffer is wrapping, get first part
  if ((bufferStart + copyLeft) > bufferSize) {
    PINDEX toCopy = bufferSize - bufferStart;

    Mix(data, (BYTE *)((unsigned long)buffer.GetPointer() + bufferStart), toCopy, channels, echoLevel, sampleRate);

    data        += toCopy;
    bufferLen   -= toCopy;
    copyLeft    -= toCopy;
    bufferStart = 0;
  }

  // get the remainder of the buffer
  if (copyLeft > 0) {

    Mix(data, (BYTE *)((unsigned long)buffer.GetPointer() + bufferStart), copyLeft, channels, echoLevel, sampleRate);

    bufferLen -= copyLeft;
    bufferStart = (bufferStart + copyLeft) % bufferSize;
  }
}

void ConferenceConnection::Mix(BYTE * dst, const BYTE * src, PINDEX count, PINDEX /*channels*/, unsigned short echoLevel, unsigned sampleRate)
{
  PINDEX i = count >> 1;
  do
  {
    short dstVal = *(short *)dst;
    short srcVal = *(short *)src;
    int newVal = dstVal;                                     // 16-bit to 32-bit, signed
    newVal += srcVal;                                        // mix
    if     (newVal >  0x7fff) *(short *)dst =  0x7fff;       // 16-bit limiter "+"
    else if(newVal < -0x8000) *(short *)dst = -0x8000;       // 16-bit limiter "-"
    else                      *(short *)dst = (short)newVal;
    src += 2;
    dst += 2;
  } while (--i);
}

///////////////////////////////////////////////////////////////

MCULock::MCULock()
{
  closing = FALSE;
  count = 0;
}

BOOL MCULock::Wait(BOOL hard)
{
  mutex.Wait();
  if (hard)
    return TRUE;

  BOOL ret = TRUE;
  if (!closing)
    count++;
  else
    ret = FALSE;

  mutex.Signal();
  return ret;
}

void MCULock::Signal(BOOL hard)
{
  if (hard) {
    mutex.Signal();
    return;
  }

  mutex.Wait();
  if (count > 0)
    count--;
  if (closing)
    closeSync.Signal();
  mutex.Signal();
}

void MCULock::WaitForClose()
{
  mutex.Wait();
  closing = TRUE;
  BOOL wait = count > 0;
  mutex.Signal();
  while (wait) {
    closeSync.Wait();
    mutex.Wait();
    wait = count > 0;
    mutex.Signal();
  }
}
