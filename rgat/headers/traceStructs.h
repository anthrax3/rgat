/*
Copyright 2016-2017 Nia Catlin

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
Structures used to represent the disassembly
*/
#pragma once
#include "stdafx.h"
#include "edge_data.h"
#include "traceConstants.h"
//#include "OSspecific.h"
#include "b64.h"

/*
Pinched from Boost
http://stackoverflow.com/questions/7222143/unordered-map-hash-function-c

get_edge has shown erratic performance with map. experiment further.
*/
template <class T>
inline void hash_combine(std::size_t & seed, const T & v)
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std
{
	template<typename S, typename T> struct hash<pair<S, T>>
	{
		inline size_t operator()(const pair<S, T> & v) const
		{
			size_t seed = 0;
			::hash_combine(seed, v.first);
			::hash_combine(seed, v.second);
			return seed;
		}
	};
}

typedef unordered_map<NODEPAIR, edge_data> EDGEMAP;

typedef void * TRACERECORDPTR;

//extern nodes this node calls. useful for 'call eax' situations
struct CHILDEXTERN 
{
	NODEINDEX vertid;
	CHILDEXTERN *next;
};

struct EXTERNCALLDATA {
	NODEPAIR edgeIdx;
	ARGLIST argList;
};



struct INS_DATA 
{
	void *bb_ptr;
	string mnemonic;
	string op_str;
	//store all the basic blocks this instruction is a member of
	vector<pair<MEM_ADDRESS, BLOCK_IDENTIFIER>> blockIDs;
	/* memory/speed tradeoff 
	1.construct every frame and save memory 
	2.construct at disassemble time and improve render speed
	*/
	string ins_text; 
	eNodeType itype;
	bool conditional = false;
	bool dataEx = false;
	bool hasSymbol = false;

	MEM_ADDRESS address;
	unsigned int numbytes;
	MEM_ADDRESS condTakenAddress;
	MEM_ADDRESS condDropAddress;
	//thread id, vert idx TODO: make unsigned
	unordered_map<PID_TID, NODEINDEX> threadvertIdx;
	unsigned int modnum;
	unsigned int mutationIndex;

	//this was added later, might be worth ditching other stuff in exchange
	string opcodes;
};

typedef vector<INS_DATA *> INSLIST;

struct BB_DATA {
	INSLIST inslist;
	unsigned int modnum;
	//list of threads that call this BB
	//inside is list of the threads verts that call it
	//it can exist multiple times on map so caller->this is listed
	//  tid     
	map <PID_TID, EDGELIST> thread_callers;

	bool hasSymbol = false;
};

struct FUNCARG {
	int argno;  //index
	char *argstr; //content
};

class PROCESS_DATA 
{
public:
	PROCESS_DATA(int binaryBitWidth) { bitwidth = binaryBitWidth;  }
	~PROCESS_DATA() { };

	bool get_sym(unsigned int modNum, MEM_ADDRESS addr, string *sym);
	bool get_modpath(unsigned int modNum, boost::filesystem::path *path);

	void kill() { if (running) { killed = true; } }

	bool should_die() { return killed; }
	bool is_running() { return running; }
	void set_running(bool r) { running = r; }
	bool get_extern_at_address(MEM_ADDRESS address, BB_DATA **BB, int attempts = 1);
	bool is_process(PID_TID testpid, int testID);
	bool load(const rapidjson::Document& saveJSON, TRACERECORDPTR trace);
	INSLIST* getDisassemblyBlock(MEM_ADDRESS blockaddr, BLOCK_IDENTIFIER blockID, bool *dieFlag, BB_DATA **externBlock);

	PID_TID PID = -1;
	int randID; //to distinguish between processes with identical PIDs

	map <int, boost::filesystem::path>modpaths;
	map <int, pair<MEM_ADDRESS, MEM_ADDRESS>> modBounds;
	map <int, std::map<MEM_ADDRESS, string>>modsymsPlain;

#ifdef XP_COMPATIBLE
	HANDLE disassemblyMutex = CreateMutex(NULL, false, NULL);
#else
	SRWLOCK disassemblyRWLock = SRWLOCK_INIT;
	SRWLOCK externCallerRWLock = SRWLOCK_INIT;
#endif

	//https://msdn.microsoft.com/en-us/library/78t98006.aspx
	inline void getDisassemblyReadLock(){
#ifdef XP_COMPATIBLE
			obtainMutex(disassemblyMutex, 6396);
#else
			AcquireSRWLockShared(&disassemblyRWLock);
#endif
		}
	inline void dropDisassemblyReadLock()	{
#ifdef XP_COMPATIBLE
		dropMutex(disassemblyMutex);
#else
		ReleaseSRWLockShared(&disassemblyRWLock);
#endif
	}
	inline void getDisassemblyWriteLock()	{

#ifdef XP_COMPATIBLE 
		obtainMutex(disassemblyMutex, 1002);
#else
		AcquireSRWLockExclusive(&disassemblyRWLock);
#endif
	}

	inline void dropDisassemblyWriteLock()	{
#ifdef XP_COMPATIBLE 
		dropMutex(disassemblyMutex);
#else
		ReleaseSRWLockExclusive(&disassemblyRWLock);
#endif
	}

	void getExternDictReadLock();
	void getExternDictWriteLock();
	void dropExternDictReadLock();
	void dropExternDictWriteLock();
	void getExternCallerReadLock();
	void getExternCallerWriteLock();
	void dropExternCallerReadLock();
	void dropExternCallerWriteLock();

	//maps instruction addresses to all data about it
	map <MEM_ADDRESS, INSLIST> disassembly;

	//list of basic blocks
	//   address		    blockID			instructionlist
	map <MEM_ADDRESS, map<BLOCK_IDENTIFIER, INSLIST *>> blocklist;

	map <int,int> activeMods;
	map <MEM_ADDRESS, BB_DATA *> externdict;
	int bitwidth;

	//graph data for each thread in process, void* because t_g_d header causes build freakout
	//map <PID_TID, PROTOGRAPH_CASTPTR> protoGraphs;
	//map <PID_TID, PLOTTEDGRAPH_CASTPTR> plottedGraphs;
	//vector<PROCESS_DATA *> children;
	TRACERECORDPTR tracePtr = NULL;

private:
	bool loadSymbols(const rapidjson::Value& saveJSON);
	bool loadModulePaths(const rapidjson::Value& processDataJSON);
	bool loadDisassembly(const rapidjson::Value& saveJSON);
	bool loadBasicBlocks(const rapidjson::Value& saveJSON);
	bool loadExterns(const rapidjson::Value& processDataJSON);

private:
#ifdef XP_COMPATIBLE
	HANDLE externDictMutex = CreateMutex(NULL, false, NULL);
#else
	SRWLOCK externDictRWLock = SRWLOCK_INIT;
#endif

	bool running = true;
	bool killed = false;
	bool dieSlowly = false;
};

struct EXTTEXT {
	unsigned int framesRemaining;
	float yOffset;
	string displayString;
};
