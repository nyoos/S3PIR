#pragma once
#include <cstdint>
#include "cryptopp/modes.h"
#include "cryptopp/osrng.h"

#include "server.h"
#include "utils.h"

typedef unsigned __int128 uint128_t;

// Client class for the one server variant.
class OneSVClient
{
public:
  //  LogN: Size of the database given in log10.
  // EntryB: Number of bits in a single entry. 
	OneSVClient(uint32_t LogN, uint32_t EntryB); 

	// Runs the offline phase. Simulates streaming the entire DB one partition at a time.
	void Offline(OneSVServer &server);

	// Runs the online phase with a single query. 
	void Online(OneSVServer &server, uint32_t query, uint64_t *result);

private:

	uint16_t NextDummyIdx();
	uint16_t prfDummyIndices [8]; // Stores dummy indices to send to server
	uint64_t dummyIdxUsed;

	uint32_t N; // Number of database entires
	uint32_t B; // Size of one entry is B * 8 bytes
	uint32_t Q;	// Number of queries made since offline phase
	uint32_t BackupUsedAgain;
	uint32_t EntrySize; 
	
	uint32_t PartNum; // Number of partitions = sqrt(N)
	uint32_t PartSize; // Number of entries in one partition
	uint32_t lambda; // Correctness parameter
	uint32_t M; // Number of hints

	// Hints are stored as arrays. Each hint consists of a HintID, a parity, an extra partition + offset, and a cutoff for the PRF value.
	uint32_t *HintID;	// Array of hintIDs for each hint.
	uint32_t *SelectCutoff;	// Array storing the PRF cutoff value for each hint.
	uint64_t *Parity; // Array of parities for each hint.
	uint16_t *ExtraPart; // Array storing the extra partition for each hint
	uint16_t *ExtraOffset; // Array storing the extra offset for each hint.
	bool *FlipCutoff; // Array storing the indicator bit for each hint. 
	uint64_t *DBPart;	// Streamed partition
	uint32_t *prfSelectVals; 

  // Arrays used to send and receive data from servers
	bool *bvec;			// Select bits to send to server. 0 selects that index to be included in parity b0. 1 selects that index to be included in parity b1. 
	uint32_t *Svec; 	// Indices sent to server
	uint64_t *Response_b0; // Parity of entries with select bit = 0 received from the server
	uint64_t *Response_b1; // Parity of entries with select bit = 1 received from the server
	uint64_t *tmpEntry; // simulating a fake server 

	PRFHintID prf;

};

/* 
Client class for the two server variant.
In the two server variant, client does not need backup hints.
The second server is used to generate hints and to replenish hints.
*/
class TwoSVClient
{
public:
	TwoSVClient(uint32_t LogN, uint32_t EntryB); 
	/* Runs the offline phase with the offline server. */
	void Offline(TwoSVServer & offline_server);
	/* Runs a single query with the online server, then replenishes a hint with the offline server. 
	Params:
		online_server: server used to query
		offline_server: server used to replenish hint
		query: database index of the desired entry
		result: value of the desired entry
	*/
	void Online(TwoSVServer & online_server, TwoSVServer & offline_server, uint32_t query, uint64_t *result);

private:
	uint16_t NextDummyIdx();
	uint16_t prfDummyIndices [8]; // Stores dummy indices to send to server
	uint64_t dummyIdxUsed;

	uint32_t N; // Number of database entries
	uint32_t B; // Size of one entry is B * 8 bytes
	
	uint32_t PartNum; // Number of partitions = sqrt(N)
	uint32_t PartSize; // Number of entries in one partition
	uint32_t lambda; // Correctness parameter
	uint32_t M; // Number of hints
	uint64_t LastHintID; // Last hint ID used

	// Hints are stored as arrays. Each hint consists of a HintID, a parity, an indicator bit, an extra partition + offset, and a cutoff for the PRF value.
	uint32_t *HintID;	// Array of hintIDs for each hint.
	uint64_t *Parity; // Array of parities for each hint.
	uint8_t *IndicatorBit; // Array storing the indicator bit for each hint. (See algorithm 3.) Each uint8_t packs 8 indicator bits.
	uint16_t *ExtraPart;	// Array storing the extra partition for each hint
	uint16_t *ExtraOffset; // Array storing the extra offset for each hint
	uint32_t *SelectCutoff; // Array storing the PRF cutoff value for each hint.
	PRFPartitionID prf;

  // Arrays used to send and receive data from servers
	bool *bvec;			// Select bits to send to server. 0 selects that index to be included in parity b0. 1 selects that index to be included in parity b1. 
	uint32_t *Svec; 	// Database indices sent to server.
	uint64_t *Response_b0; // Parity of entries with select bit = 0 received from server.
	uint64_t *Response_b1; // Parity of entries with select bit = 1 received from server.
	uint64_t *tmpEntry; // simulating a fake server 
};