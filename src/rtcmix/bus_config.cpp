/* RTcmix  - Copyright (C) 2000  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ugens.h>
#include <bus.h>
#include <algorithm>
#include "BusSlot.h"
#include <RTcmix.h>
#include <RTThread.h>
#include "prototypes.h"
#include "InputFile.h"
#include <lock.h>
#include <RTOption.h>
  
//#define PRINTPLAY
//#define DEBUG
//#define PRINTALL
//#define DEBUGMEM

#ifdef DEBUGMEM
#define MPRINT(string, ptr) RTPrintf(string, (unsigned) ptr)
#else
#define MPRINT(string, ptr)
#endif

struct BusQueue {
	BusQueue(char *name, BusSlot *theQueue);
	~BusQueue();
	char *instName() { return inst_name; }
	char *inst_name;
	BusSlot *slot;
	BusQueue *next;
};

//
// BusSlot "class" methods
//

BusSlot::BusSlot(int inBusCount)
	: next(NULL), prev(NULL), in(new short[inBusCount]), out(new short[inBusCount]), auxin(new short[inBusCount]), auxout(new short[inBusCount]),
	  in_count(0), out_count(0), auxin_count(0), auxout_count(0)
{
	for (int n=0; n<inBusCount; ++n)
	    in[n] = out[n] = auxin[n] = auxout[n] = 0;
}

BusSlot::~BusSlot()
{
	delete [] auxout;
	delete [] auxin;
	delete [] out;
	delete [] in;
	MPRINT("BusSlot 0x%x destroyed\n", this);
}

//
// BusQueue class methods
//

BusQueue::BusQueue(char *name, BusSlot *theSlot)
		: inst_name(strdup(name)), slot(theSlot), next(NULL)
{
	slot->ref();
}

BusQueue::~BusQueue()
{
	MPRINT("BusQueue 0x%x destroyed\n", this);
	free(inst_name);
	slot->unref();
}

// Local classes for configuration checking

struct CheckNode : public RefCounted {
   CheckNode(int inBusCount) : bus_list(new short[inBusCount]), bus_count(0), ownsBusList(true) {}
   CheckNode(short *list, short count) : bus_list(list), bus_count(count), ownsBusList(false) {}
   ~CheckNode() { if (ownsBusList) delete [] bus_list; MPRINT("CheckNode 0x%x destroyed\n", this); }
   short *bus_list;
   short bus_count;
   bool ownsBusList;
};

struct CheckQueue {
   CheckQueue(CheckNode *theNode) : node(theNode), next(NULL) { node->ref(); }
   ~CheckQueue() { RefCounted::unref(node); }
   CheckNode *node;
   CheckQueue *next;
};


/* Prototypes (in order)------------------------------------------------------*/
static int strtoint(char*, int *);  /* Helper */
static void print_bus_slot(BusSlot *);  /* Debugging */

/* ------------------------------------------------------------- strtoint --- */
static inline int
strtoint(char *str, int *num)
{
   long  along;
   char  *pos;

   pos = NULL;
   errno = 0;
   along = strtol(str, &pos, 10);
   if (pos == str)                           /* no digits to convert */
      return -1;
   if (errno == ERANGE)                      /* overflow or underflow */
      return -1;

   *num = (int)along;
   return 0;
}

/* ------------------------------------------------------- print_parents -----*/
void
RTcmix::print_parents() {
  int i;
  RTPrintfCat("Aux buses w/o aux inputs:  ");
  for(i=0;i<busCount;i++) {
	BusConfig *bus = &BusConfigs[i];
	pthread_mutex_lock(&aux_in_use_lock);
	if (bus->AuxInUse) {
	  pthread_mutex_lock(&has_parent_lock);
	  if (!bus->HasParent) {
		RTPrintfCat(" %d",i);
	  }
	  pthread_mutex_unlock(&has_parent_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  RTPrintf("\n");
}

/* ------------------------------------------------------ print_children -----*/
void
RTcmix::print_children() {
  int i;
  RTPrintfCat("Aux buses w/o aux outputs:  "); 
  for(i=0;i<busCount;i++) {
	BusConfig *bus = &BusConfigs[i];
	pthread_mutex_lock(&aux_in_use_lock);
	if (bus->AuxInUse) {
	  pthread_mutex_lock(&has_child_lock);
	  if (!bus->HasChild) {
		RTPrintfCat(" %d",i);
	  }
	  pthread_mutex_unlock(&has_child_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  RTPrintf("\n");
}

/* ------------------------------------------------------- print_bus_slot --- */
static void
print_bus_slot(BusSlot *bs)
{
   int i;

   RTPrintfCat("\n   in_count=%d :", bs->in_count);
   for (i = 0; i < bs->in_count; i++)
      RTPrintfCat(" %d", bs->in[i]);
   RTPrintfCat("\n   out_count=%d :", bs->out_count);
   for (i = 0; i < bs->out_count; i++)
      RTPrintfCat(" %d", bs->out[i]);
   RTPrintfCat("\n   auxin_count=%d :", bs->auxin_count);
   for (i = 0; i < bs->auxin_count; i++)
      RTPrintfCat(" %d", bs->auxin[i]);
   RTPrintfCat("\n   auxout_count=%d :", bs->auxout_count);
   for (i = 0; i < bs->auxout_count; i++)
      RTPrintfCat(" %d", bs->auxout[i]);
   RTPrintf("\n");
}

/* ----------------------------------------------------- print_bus_config --- */
/* Prints config from Inst. point of view */
ErrCode
RTcmix::print_inst_bus_config() {
   BusQueue *qEntry;
   BusSlot *check_slot;

   pthread_mutex_lock(&inst_bus_config_lock);
   qEntry = Inst_Bus_Config;
   pthread_mutex_unlock(&inst_bus_config_lock);

   while (qEntry) {

	  RTPrintfCat("%s",qEntry->instName());
	  check_slot = qEntry->slot;
	  
	  if (check_slot == NULL) {
		 RTPrintf("done\n");
		 return NO_ERR;
	  }
	  
	  while (check_slot) {
		 print_bus_slot(check_slot);
		 check_slot = check_slot->next;
	  }
	  qEntry = qEntry->next; 
   }
   return NO_ERR;
}

/* ----------------------------------------------------- print_play_order --- */
void
RTcmix::print_play_order() {
  int i;
  RTPrintfCat("Output buffer playback order:  ");
  for(i=0;i<busCount;i++) {
	pthread_mutex_lock(&aux_to_aux_lock);
	if (AuxToAuxPlayList[i] != -1) {
	  RTPrintfCat(" %d",AuxToAuxPlayList[i]);
	}
	pthread_mutex_unlock(&aux_to_aux_lock);
  }
  RTPrintf("\n");
}

static Bool Visited[MAXBUS];

/* ------------------------------------------------ check_bust_inst_config -- */
/* Parses bus graph nodes */

ErrCode
RTcmix::check_bus_inst_config(BusSlot *slot, Bool visit) {
	int i,j,aux_ctr,out_ctr;
	short *in_check_list;
	short in_check_count;
	CheckQueue *in_check_queue,*last;
	Bool Checked[MAXBUS];
	short r_p_count=0;

	/* If we haven't gotten a config yet ... allocate the graph array */
	/* and the playback order list */
	Bus_Config_Status.lock();
	if (Bus_Config_Status == NO) {
		for (i=0;i<busCount;i++) {
			BusConfig *bus = &BusConfigs[i];
			CheckNode *t_node = new CheckNode(busCount);
			pthread_mutex_lock(&bus_in_config_lock);
			bus->In_Config = t_node;
			t_node->ref();
			pthread_mutex_unlock(&bus_in_config_lock);
		}
		Bus_Config_Status = YES;
	}
	Bus_Config_Status.unlock();

	aux_ctr = out_ctr = 0;
	j=0;
	for(i=0;i<busCount;i++) {
		BusConfig *bus = &BusConfigs[i];
		if (visit)
			Visited[i] = NO;
		Checked[i] = NO;
		pthread_mutex_lock(&revplay_lock);
		bus->RevPlay = -1;
		pthread_mutex_unlock(&revplay_lock);
		pthread_mutex_lock(&out_in_use_lock);
		if (bus->OutInUse) {  // DJT For scheduling
			pthread_mutex_lock(&to_out_lock);
			ToOutPlayList[out_ctr++] = i;
			pthread_mutex_unlock(&to_out_lock);
		}
		pthread_mutex_unlock(&out_in_use_lock);
		pthread_mutex_lock(&aux_out_in_use_lock);
		if (bus->AuxOutInUse) {
			pthread_mutex_lock(&to_aux_lock);
			ToAuxPlayList[aux_ctr++] = i;
			pthread_mutex_unlock(&to_aux_lock);
		}
		pthread_mutex_unlock(&aux_out_in_use_lock);
	}

	/* Put the slot being checked on the list of "to be checked" */
	CheckNode *t_node = new CheckNode(slot->auxin, slot->auxin_count);
	last = in_check_queue = new CheckQueue(t_node);
	CheckQueue *savedQueueHead = in_check_queue;

	/* Go down the list of things (nodes) to be checked */
	while (in_check_queue) {
		CheckNode *t_check_node = in_check_queue->node;
		in_check_list = t_check_node->bus_list;
		in_check_count = t_check_node->bus_count;

		for (i=0;i<in_check_count;i++) {
			short t_in = in_check_list[i];

			/* Compare to each of the input slot's output channels */
			for (j=0;(j<slot->auxout_count) && (!Checked[t_in]);j++) {
				const short t_out = slot->auxout[j];
#ifdef PRINTALL
				RTPrintf("check_bus_inst_config: checking in=%d out=%d\n",t_in,t_out);
#endif
				/* If they're equal, then return the error */
				if (t_in == t_out) {
					rterror(NULL, "bus_config loop ... config not allowed.\n");
					return LOOP_ERR;
				}
			}
			if (!Checked[t_in]) {
				Checked[t_in] = YES;
			}

			/* If this input channel has other input channels */
			/* put them on the list "to be checked" */

			pthread_mutex_lock(&bus_in_config_lock);
			if ((BusConfigs[t_in].In_Config->bus_count > 0) && !Visited[t_in]) {
#ifdef PRINTALL
				RTPrintf("check_bus_inst_config: adding Bus[%d] to list\n",t_in);
#endif
				pthread_mutex_lock(&has_parent_lock);
				if (BusConfigs[t_in].HasParent) {
#ifdef PRINTPLAY
					RTPrintf("check_bus_inst_config: RevPlay[%d] = %d\n",r_p_count,t_in);
#endif
					pthread_mutex_lock(&revplay_lock);
					BusConfigs[r_p_count++].RevPlay = t_in;
					pthread_mutex_unlock(&revplay_lock);
				}
				pthread_mutex_unlock(&has_parent_lock);
				Visited[t_in] = YES;
				CheckQueue *t_queue = new CheckQueue(BusConfigs[t_in].In_Config);
				last->next = t_queue;
				last = t_queue;
			}
			pthread_mutex_unlock(&bus_in_config_lock);
		}
#ifdef PRINTALL
		RTPrintf("check_bus_inst_config: popping ...\n");
#endif
		in_check_queue = in_check_queue->next;
	}

#ifdef PRINTALL
	RTPrintf("check_bus_inst_config: cleaning up\n");
#endif
	// Now clean up
	CheckQueue *queue = savedQueueHead;
	while (queue) {
		CheckQueue *next = queue->next;
		delete queue;
		queue = next;
	}
	
	return NO_ERR;
}

/* ------------------------------------------------------ insert_bus_slot --- */
/* Inserts bus configuration into structure used by insts */
/* Also inserts into bus graph */
/* Special case when called by bf_traverse->check_bus_inst_config-> */
/*     s_in set to 333 and filtered out below */
ErrCode
RTcmix::insert_bus_slot(char *name, BusSlot *slot) {
  
	short i,j,t_in_count,s_in,s_out;

	/* Insert into bus graph */
	for(i=0;i<slot->auxout_count;i++) {
		s_out = slot->auxout[i];
		pthread_mutex_lock(&aux_in_use_lock);
		BusConfig *outbus = &BusConfigs[s_out];
		if (!outbus->AuxInUse) {
			outbus->AuxInUse = YES;
		}
		pthread_mutex_unlock(&aux_in_use_lock);
		for(j=0;j<slot->auxin_count;j++) {
			s_in = slot->auxin[j];
			pthread_mutex_lock(&has_parent_lock);
			if (!outbus->HasParent && s_in != 333) {
#ifdef PRINTALL
				RTPrintf("insert_bus_slot: HasParent[%d]\n",s_out);
#endif
				outbus->HasParent = YES;
			}
			pthread_mutex_unlock(&has_parent_lock);

			pthread_mutex_lock(&bus_in_config_lock);
			t_in_count = outbus->In_Config->bus_count;
			pthread_mutex_unlock(&bus_in_config_lock);
#ifdef PRINTALL
			RTPrintf("insert_bus_slot: Inserting Bus_In[%d] = %d\n",s_out,s_in);
#endif
			if (s_in != 333) {
				pthread_mutex_lock(&bus_in_config_lock);
				outbus->In_Config->bus_list[t_in_count] = s_in;
				outbus->In_Config->bus_count++;

// BGG -- my bus-wrapping hackeroo!  go brad go!  :-)
				if (outbus->In_Config->bus_count >= busCount)
					outbus->In_Config->bus_count = 0;
				pthread_mutex_unlock(&bus_in_config_lock);
				pthread_mutex_lock(&has_child_lock);
				BusConfigs[s_in].HasChild = YES;
				pthread_mutex_unlock(&has_child_lock);
				pthread_mutex_lock(&aux_in_use_lock);
				BusConfigs[s_in].AuxInUse = YES;
				pthread_mutex_unlock(&aux_in_use_lock);
			}
		}
	}

	/* Create initial node for Inst_Bus_Config */
	pthread_mutex_lock(&inst_bus_config_lock);
	if (Inst_Bus_Config == NULL) {
		Inst_Bus_Config = new BusQueue(name, slot);
		pthread_mutex_unlock(&inst_bus_config_lock);
		return NO_ERR;
	}

	BusQueue *qEntry = Inst_Bus_Config;
	pthread_mutex_unlock(&inst_bus_config_lock);

	Lock lock(&inst_bus_config_lock);	// unlocks when out of scope
	/* Traverse down each list */
	while (qEntry) {	
		/* If names match, then put onto the head of the slot's list */
		if (strcmp(qEntry->instName(), name) == 0) {
			BusSlot *next = qEntry->slot->next;
			// Remove our reference to this slot and replace.
			qEntry->slot->unref();
#ifdef PRINTALL
			RTPrintf("insert_bus_slot: replacing slot entry for '%s'\n", name);
#endif
			slot->next = next;
			qEntry->slot = slot;
			slot->ref();
			return NO_ERR;
		}

		/* We've reached the end ... so put a new node on with inst's name */
		if (qEntry->next == NULL) {
			qEntry->next = new BusQueue(name, slot);
  			return NO_ERR;
		}
		qEntry = qEntry->next;
	}
	return NO_ERR;
}


/* ----------------------------------------------------- bf_traverse -------- */
/* sets fictitious parent node to 333 */
/* filtered out in insert() */
void
RTcmix::bf_traverse(int bus, Bool visit) {
#ifdef PRINTPLAY
  RTPrintf("entering bf_traverse(%d)\n", bus);
#endif
  BusSlot *temp = new BusSlot(busCount);
  temp->ref();
  temp->auxin[0] = bus;
  temp->auxin_count=1;
  temp->auxout[0] = 333;
  temp->auxout_count=1;
  check_bus_inst_config(temp, visit);
  temp->unref();
#ifdef PRINTPLAY
  RTPrintf("exiting bf_traverse(%d)\n", bus);
#endif
}

/* ----------------------------------------------------- create_play_order -- */
void
RTcmix::create_play_order() {
  int i,j;
  Bool visit = YES;
  short aux_p_count = 0;

  /* Put all the parents on */
  for(i=0;i<busCount;i++) {
	BusConfig *bus = &BusConfigs[i];
	pthread_mutex_lock(&aux_in_use_lock);
	if (bus->AuxInUse) {
	  pthread_mutex_lock(&has_parent_lock);
	  if (!bus->HasParent) {
#ifdef PRINTPLAY
		RTPrintf("create_play_order: AuxPlay[%d] = %d\n",aux_p_count,i);
#endif
		pthread_mutex_lock(&aux_to_aux_lock);
		AuxToAuxPlayList[aux_p_count++] = i;
		pthread_mutex_unlock(&aux_to_aux_lock);
	  }
	  pthread_mutex_unlock(&has_parent_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
  for (i=0;i<busCount;i++) {
	BusConfig *bus = &BusConfigs[i];
	pthread_mutex_lock(&aux_in_use_lock);
	if (bus->AuxInUse) {
	  pthread_mutex_lock(&has_child_lock);
	  if (!bus->HasChild) {
		bf_traverse(i,visit);
		if (visit) 
		  visit = NO;
		for (j=busCount-1;j>=0;j--) {
		  pthread_mutex_lock(&revplay_lock);
		  if (BusConfigs[j].RevPlay != -1) {
#ifdef PRINTPLAY
			RTPrintf("create_play_order: AuxPlay[%d](%d) = Rev[%d](%d)\n",
					aux_p_count,AuxToAuxPlayList[aux_p_count],j,RevPlay[j]);
#endif
			pthread_mutex_lock(&aux_to_aux_lock);
			AuxToAuxPlayList[aux_p_count++] = BusConfigs[j].RevPlay;
			pthread_mutex_unlock(&aux_to_aux_lock);
		  }
		  pthread_mutex_unlock(&revplay_lock);
		}
	  }
	  pthread_mutex_unlock(&has_child_lock);
	}
	pthread_mutex_unlock(&aux_in_use_lock);
  }
}

/* ------------------------------------------------------- get_bus_config --- */
/* Given an instrument name, return a pointer to the most recently
   created BusSlot node for that instrument name. If no instrument name
   match, return a pointer to the default node.
*/
BusSlot *
RTcmix::get_bus_config(const char *inst_name)
{
   BusSlot  *slot, *default_bus_slot;
   BusQueue *q;
   ErrCode     err;
   int index,in_chans,i;

   assert(inst_name != NULL);

   slot = NULL;

   Lock lock(&bus_slot_lock);	// unlocks when out of scope

   /* Maybe also need to lock q since it's accessing a BusSlot */
   /* that intraverse might also be checking? */
   /* But the values don't change, so I don't see why */

   pthread_mutex_lock(&inst_bus_config_lock);
   for (q = Inst_Bus_Config; q; q = q->next) {
	 if (strcmp(inst_name, q->instName()) == 0) {
	   pthread_mutex_unlock(&inst_bus_config_lock);   
	   return q->slot;
	 }
   }
   pthread_mutex_unlock(&inst_bus_config_lock);
   
   /* Default bus_config for backwards compatibility with < 3.0 scores */
   
   rtcmix_advise(NULL, "No bus_config defined, setting default (in/out).");
   
   /* Some init stuff normally done in check_bus_inst_config */
   Bus_Config_Status.lock();
   if (Bus_Config_Status == NO) {
	 for (i=0;i<busCount;i++) {
	   BusConfig *bus = &BusConfigs[i];
	   pthread_mutex_lock(&aux_to_aux_lock);
	   AuxToAuxPlayList[i] = -1;
	   pthread_mutex_unlock(&aux_to_aux_lock);
	   pthread_mutex_lock(&to_aux_lock);
	   ToAuxPlayList[i] = -1;
	   pthread_mutex_unlock(&to_aux_lock);
	   pthread_mutex_lock(&to_out_lock);
	   ToOutPlayList[i] = -1;
	   pthread_mutex_unlock(&to_out_lock);
	   pthread_mutex_lock(&out_in_use_lock);
	   bus->OutInUse = NO;
	   pthread_mutex_unlock(&out_in_use_lock);
	   // Added this initialization as well -- DS 5/2005
	   CheckNode *t_node = new CheckNode(busCount);
	   pthread_mutex_lock(&bus_in_config_lock);
	   bus->In_Config = t_node;
	   t_node->ref();
	   pthread_mutex_unlock(&bus_in_config_lock);
	 }
	 Bus_Config_Status = YES;
   }
   Bus_Config_Status.unlock();

   for(i=0;i<NCHANS;i++) {
	 pthread_mutex_lock(&out_in_use_lock);
	 BusConfigs[i].OutInUse = YES;
	 pthread_mutex_unlock(&out_in_use_lock);
	 pthread_mutex_lock(&to_out_lock);
	 ToOutPlayList[i] = i;
	 pthread_mutex_unlock(&to_out_lock);
   }

   default_bus_slot = new BusSlot(busCount);
   /* Grab input chans from file descriptor table */
   index = get_last_input_index();
   /* Otherwise grab from audio device, if active */
   if (index == -1) {
	 if (RTOption::record() && RTOption::play())
	   in_chans = NCHANS;
	 else
	   in_chans = 0;
   }
   else {
     in_chans = inputFileTable[index].channels();
     assert(in_chans > 0);
   }
   
   default_bus_slot->in_count = in_chans;
   default_bus_slot->out_count = NCHANS;
   
   for(i=0;i<in_chans;i++) {
	 default_bus_slot->in[i] = i;
   }
   for(i=0;i<NCHANS;i++) {
	 default_bus_slot->out[i] = i;
   }

   err = check_bus_inst_config(default_bus_slot, YES);
   if (!err) {
      err = insert_bus_slot((char *)inst_name, default_bus_slot);
   }
   if (err) {
		die("bus_config", "get_bus_config failed, this is not good");
		RTExit(SYSTEM_ERROR);        /* This is probably what user wants? */
	}
	
	// Print out the default bus config (if verbosity permits)
	
	char buslist[64];
	switch (default_bus_slot->in_count) {
		case 0:
			sprintf(buslist, "() => ");
			break;
		case 1:
			sprintf(buslist, "(in 0) => ");
			break;
		default:
			sprintf(buslist, "(in 0-%d) => ", default_bus_slot->in_count - 1);
			break;
	}
	strcat(buslist, inst_name);
	if (default_bus_slot->out_count == 1)
		strcat(buslist, " => (out 0)");
	else
		sprintf(buslist + strlen(buslist), " => (out 0-%d)", default_bus_slot->out_count - 1);
	
	rtcmix_advise(NULL, "default: %s\n", buslist);

   return default_bus_slot;
}

/* ------------------------------------------------------- addToBus --------- */
/* This is called by each instrument during addout() to insert a request for a mix.
   All requests are mixed at the same time via mixToBus().
 */

#ifdef MULTI_THREAD

void
RTcmix::addToBus(BusType type, int bus, BufPtr src, int offset, int endfr, int chans)
{
    mixVectors[RTThread::GetIndexForThread()].push_back(
						MixData(
								src,
								(type == BUS_AUX_OUT) ? aux_buffer[bus] + offset : out_buffer[bus] + offset,
								endfr - offset,
								chans)
                        );
	
}

void
RTcmix::mixOperation(MixData &m)
{
    BufPtr src = m.src;
    BufPtr dest = m.dest;
    const int framesOverFour = m.frames >> 2;
    const int framesRemaining = m.frames - (framesOverFour << 2);
    const int chans = m.channels;
    const int chansx2 = chans << 1;
    const int chansx3 = chansx2 + chans;
    const int chansx4 = chansx2 + chansx2;
    for (int n = 0; n < framesOverFour; ++n) {
        dest[0] += src[0];
        dest[1] += src[chans];
        dest[2] += src[chansx2];
        dest[3] += src[chansx3];
        dest += 4;
        src += chansx4;
    }
    for (int n = 0; n < framesRemaining; ++n) {
        dest[n] += *src;
        src += chans;
    }
}

void
RTcmix::mixToBus()
{
    // Mix all vectors from each thread down to the final mix buses
    for (int i = 0; i < RT_THREAD_COUNT; ++i) {
        std::vector<MixData> &vector = mixVectors[i];
        std::for_each(vector.begin(), vector.end(), mixOperation);
        vector.clear();
    }
}

#else

/* ------------------------------------------------------- addToBus --------- */
/* This is called by each instrument during addout() to mix itself into bus. */

void
RTcmix::addToBus(BusType type, int bus, BufPtr src, int offset, int endfr, int chans)
{
	register BufPtr dest;
	
	if (type == BUS_AUX_OUT) {
		dest = aux_buffer[bus];
	}
	else {
		dest = out_buffer[bus];
	}
	assert(dest != NULL);
	for (int frame = offset; frame < endfr; frame++) {
		dest[frame] += *src;
		src += chans;
	}
}

#endif	// MULTI_THREAD

/* ------------------------------------------------------- parse_bus_chan --- */
static ErrCode
parse_bus_chan(char *numstr, int *startchan, int *endchan, int maxBus)
{
   char  *p;

   if (strtoint(numstr, startchan) == -1)
      return INVAL_BUS_CHAN_ERR;

   p = strchr(numstr, '-');
   if (p) {
      p++;                                           /* skip over '-' */
      if (strtoint(p, endchan) == -1)
         return INVAL_BUS_CHAN_ERR;
   }
   else
      *endchan = *startchan;

	/* NOTE: with the current code, only maxBus-1 channels are allowed */
	if (*startchan >= maxBus || *endchan >= maxBus)
		return INVAL_BUS_CHAN_ERR;

   return NO_ERR;
}

/* ------------------------------------------------------- parse_bus_name --- */
ErrCode
parse_bus_name(char *busname, BusType *type, int *startchan, int *endchan, int maxBus)
{
   char     *p;
   ErrCode  status = NO_ERR;

   if (busname == NULL)
	   status = INVAL_BUS_ERR;
   else switch (busname[0]) {
      case 'i':                                      /* "in*" */
         *type = BUS_IN;
         p = &busname[2];                            /* skip over "in" */
         status = parse_bus_chan(p, startchan, endchan, maxBus);
         break;
      case 'o':                                      /* "out*" */
         *type = BUS_OUT;
         p = &busname[3];                            /* skip over "out" */
         status = parse_bus_chan(p, startchan, endchan, maxBus);
         break;
      case 'a':                                      /* "aux*" */
         if (strchr(busname, 'i'))
            *type = BUS_AUX_IN;
         else if (strchr(busname, 'o'))
            *type = BUS_AUX_OUT;
         else {
             rtcmix_warn("bus_config", "Invalid bus specifier: '%s'", busname);
            return INVAL_BUS_ERR;
         }
         p = &busname[3];                            /* skip over "aux" */
         status = parse_bus_chan(p, startchan, endchan, maxBus);
         break;
	  case 'c':										 /* "chain*" */
		p = &busname[5];                            /* skip over "chain" */
		if (strchr(p, 'i'))
		   *type = BUS_NONE_IN;
		else if (strchr(p, 'o'))
		   *type = BUS_NONE_OUT;
        else {
            rtcmix_warn("bus_config", "Invalid bus specifier: '%s'", busname);
		   return INVAL_BUS_ERR;
        }
		status = parse_bus_chan(p, startchan, endchan, maxBus);
		break;
      default:
	  	 rtcmix_warn("bus_config", "Invalid bus specifier: '%s'", busname);
         return INVAL_BUS_ERR;
   }
   if (status != NO_ERR)
		rtcmix_warn("bus_config", "Invalid bus specifier: '%s'%s", busname, (status == INVAL_BUS_CHAN_ERR) ? ": exceeded bus count (or negative bus)":"");
   return status;
}

// D.S. This is now a static function

/* ----------------------------------------------------------- bus_config --- */
double 
RTcmix::bus_config(double p[], int n_args)
{
   ErrCode     err;
   int         i, j, k, startchan, endchan, chain_incount=0, chain_outcount=0;
   char        *instname, *busname;
   BusType     type;
   BusSlot     *bus_slot;
   char		   inbusses[80], outbusses[80];	// for verbose message

    if (n_args < 2) {
      die("bus_config", "Wrong number of args.");
        RTExit(PARAM_ERROR);
    }

   if (!rtsetparams_was_called()) {
#ifdef EMBEDDED
        die("bus_config", "You need to start the audio device before doing this.");
#else
        die("bus_config", "You did not call rtsetparams!");
#endif
       RTExit(PARAM_ERROR);
   }
   try {
       bus_slot = new BusSlot(busCount);
   } catch(...) {
       RTExit(MEMORY_ERROR);
   }

   inbusses[0] = outbusses[0] = '\0';
   
   Lock localLock(&bus_slot_lock);	// This will unlock when going out of scope.

   /* do the old Minc casting rigamarole to get string pointers from a double */
   instname = DOUBLE_TO_STRING(p[0]);

   for (i = 1; i < n_args; i++) {
      busname = DOUBLE_TO_STRING(p[i]);
      err = parse_bus_name(busname, &type, &startchan, &endchan, busCount);
       switch (err) {
           case INVAL_BUS_ERR:
           case INVAL_BUS_CHAN_ERR:
           case LOOP_ERR:
               RTExit(PARAM_ERROR);
           case UNKNOWN_ERR:
               RTExit(SYSTEM_ERROR);
           default:
               break;
       }
      switch (type) {
         case BUS_IN:
			if (bus_slot->in_count > 0) strcat(inbusses, ", ");
			strcat(inbusses, busname);
            if (bus_slot->auxin_count > 0) {
                die("bus_config", "Can't have 'in' and 'aux-in' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            if (chain_incount > 0) {
            	die("bus_config", "Can't have 'in' and 'chain-in' buses in same bus_config.");
                RTExit(PARAM_ERROR);
           }
            /* Make sure max channel count set in rtsetparams can accommodate
               the highest input chan number in this bus config.
            */
            if (endchan >= NCHANS) {
            	die("bus_config", "You specified %d channels in rtsetparams,\n"
            				"but this bus_config requires %d channels.",
							 NCHANS, endchan + 1);
                RTExit(PARAM_ERROR);
            }
           j = bus_slot->in_count;
            for (k = startchan; k <= endchan; k++)
               bus_slot->in[j++] = k;
            bus_slot->in_count += (endchan - startchan) + 1;
			break;
         case BUS_OUT:
			if (bus_slot->out_count > 0) strcat(outbusses, ", ");
			strcat(outbusses, busname);
            if (bus_slot->auxout_count > 0) {
                die("bus_config", "Can't have 'out' and 'aux-out' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            if (chain_outcount > 0) {
            	die("bus_config", "Can't have 'out' and 'chain-out' buses in same bus_config.");
                RTExit(PARAM_ERROR);
           }
            /* Make sure max output chans set in rtsetparams can accommodate
			   the highest output chan number in this bus config.
            */
            if (endchan >= NCHANS) {
            	die("bus_config", "You specified %d output channels in rtsetparams,\n"
							 "but this bus_config requires %d channels.",
							 NCHANS, endchan + 1);
                RTExit(PARAM_ERROR);
            }
            j = bus_slot->out_count;
            for (k = startchan; k <= endchan; k++) {
               bus_slot->out[j++] = k;
			   pthread_mutex_lock(&out_in_use_lock);
               BusConfigs[k].OutInUse = YES;  // DJT added
			   pthread_mutex_unlock(&out_in_use_lock);
            }
            bus_slot->out_count += (endchan - startchan) + 1;

            break;
         case BUS_AUX_IN:
			if (bus_slot->auxin_count > 0) strcat(inbusses, ", ");
			strcat(inbusses, busname);
            if (bus_slot->in_count > 0) {
                die("bus_config",
                    	  "Can't have 'in' and 'aux-in' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            if (chain_incount > 0) {
            	die("bus_config",
                     	 "Can't have 'chain-in' and 'aux-in' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            j = bus_slot->auxin_count;
            for (k = startchan; k <= endchan; k++)
               bus_slot->auxin[j++] = k;
            bus_slot->auxin_count += (endchan - startchan) + 1;
            break;
         case BUS_AUX_OUT:
			if (bus_slot->auxout_count > 0) strcat(outbusses, ", ");
			strcat(outbusses, busname);
            if (bus_slot->out_count > 0) {
                die("bus_config",
                  	  "Can't have 'out' and 'aux-out' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            if (chain_outcount > 0) {
            	die("bus_config",
            			"Can't have 'aux-out' and 'chain-out' buses in same bus_config.");
                RTExit(PARAM_ERROR);
            }
            j = bus_slot->auxout_count;
            for (k = startchan; k <= endchan; k++) {
               bus_slot->auxout[j++] = k;
			   pthread_mutex_lock(&aux_out_in_use_lock);
               BusConfigs[k].AuxOutInUse = YES;
			   pthread_mutex_unlock(&aux_out_in_use_lock);
            }
            bus_slot->auxout_count += (endchan - startchan) + 1;
            break;
		  case BUS_NONE_IN:
			  strcat(inbusses, busname);
			  if (bus_slot->in_count + bus_slot->auxin_count > 0) {
				  die("bus_config",
                     	 "Can't have 'chain-in' combined with any other in type in same bus_config.");
                  RTExit(PARAM_ERROR);
			  }
			  chain_incount += (endchan - startchan) + 1;
			  break;
		  case BUS_NONE_OUT:
			  strcat(outbusses, busname);
			  if (bus_slot->out_count + bus_slot->auxout_count > 0) {
				  die("bus_config",
					  	"Can't have 'chain-out' combined with any other out type in same bus_config.");
                  RTExit(PARAM_ERROR);
			  }
			  chain_outcount = (endchan - startchan) + 1;
			  break;
		 default:
		 	break;
      }
   }

   err = check_bus_inst_config(bus_slot, YES);
   if (!err) {
      err = insert_bus_slot(instname, bus_slot);
   }
   if (err) {
		die("bus_config", "couldn't configure the busses");
	   RTExit(SYSTEM_ERROR);        /* This is probably what user wants? */
	}

   /* Make sure specified aux buses have buffers allocated. */
   for (i = 0; i < bus_slot->auxin_count; i++)
      allocate_aux_buffer(bus_slot->auxin[i], bufsamps());
   for (i = 0; i < bus_slot->auxout_count; i++)
      allocate_aux_buffer(bus_slot->auxout[i], bufsamps());

	// We have to set these after all the above code to prevent chain assignments
	// from generating conflicts or bus allocations.  Setting the auxin_count allows
	// the instrument to pass input inspection (does not fail due to missing input or bus).
	
	bus_slot->auxin_count += chain_incount;
	bus_slot->out_count += chain_outcount;
	
#ifdef PRINTALL
   print_children();
   print_parents();
#endif
   create_play_order();
#ifdef PRINTPLAY
   print_play_order();
#endif
#ifdef DEBUG
   err = print_inst_bus_config();
#endif

   rtcmix_advise("bus_config", "(%s) => %s => (%s)", inbusses, instname, outbusses);
   return 0;
}

void
RTcmix::free_bus_config()
{
   for (BusQueue *q = Inst_Bus_Config; q;) {
      BusQueue *next = q->next;
      delete q;
      q = next;
   }
	Inst_Bus_Config = NULL;
    BusConfig zeroConfig;
	if (BusConfigs) {
		for (int i=0 ; i<busCount; i++) {
		   RefCounted::unref(BusConfigs[i].In_Config);
		   BusConfigs[i] = zeroConfig;
		}
	}
	memset(&Visited, 0, sizeof(Visited));
	Bus_Config_Status = NO;
}
