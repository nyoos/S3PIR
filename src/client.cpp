#include "client.h"
#include "server.h"
#include <random>
#include <algorithm>
#include <cassert>

using namespace std;
using namespace CryptoPP;

// N is supported up to 2^32. Allows us to use uint16_t to store a single offset within partition
TwoSVClient::TwoSVClient(uint32_t LogN, uint32_t EntryB):
 prf(AES_KEY) {
	assert(LogN < 32);
	assert(EntryB >= 8);
	N = 1 << LogN;
	// B is the size of one entry in uint64s
	B = EntryB / 8;

	PartNum = 1 << (LogN / 2);
	PartSize = 1 << (LogN / 2 + LogN % 2);
	lambda = LAMBDA;
	M = lambda * PartSize;

	// Allocate storage for hints
	HintID = new uint32_t [M];
	Parity = new uint64_t [M*B];
	ExtraPart = new uint16_t [M];
	ExtraOffset = new uint16_t [M];
	SelectCutoff = new uint32_t [M];
	// Pack the bits together
	IndicatorBit = new uint8_t[(M+7)/8];
	LastHintID = 0;

	dummyIdxUsed = 0;			// ever increasing to not repeat, use % 8 to index


	// Allocate memory for making requests to servers and receiving responses from servers
	bvec = new bool [PartNum];
	Svec = new uint32_t [PartNum];
	Response_b0 = new uint64_t [B];
	Response_b1 = new uint64_t [B];
	tmpEntry = new uint64_t [B];
}

void TwoSVClient::Offline(TwoSVServer & offline_server) {
	// Initialize the hint parity array to 0.
	memset(Parity, 0, sizeof(uint64_t) * B * M);
	// For offline generation the indicator bit is always set to 1.
	memset(IndicatorBit, 255, (M+7)/8);

  // Initialize hints.
	for (uint64_t j = 0; j < M; j++){
		HintID[j] = j;
	}

	offline_server.generateOfflineHints(M,Parity,ExtraPart,ExtraOffset, SelectCutoff);
	LastHintID = M;
}

uint16_t TwoSVClient::NextDummyIdx() {
	if (dummyIdxUsed % 8 == 0)	// need more dummy indices
		prf.evaluate((uint8_t*) prfDummyIndices, 0, dummyIdxUsed / 8, 0);
	return prfDummyIndices[dummyIdxUsed++ % 8]; 
}

void TwoSVClient::Online(TwoSVServer & online_server, TwoSVServer & offline_server, uint32_t query, uint64_t *result)
{
	assert(query <= N);
	uint16_t queryPartNum = query / PartSize;
	uint16_t queryOffset = query & (PartSize-1);
	uint64_t hintIndex = 0;
	bool b_indicator = 0;

	// Run Algorithm 2
  // Find a hint that has our desired query index
	for (; hintIndex < M; hintIndex++){
		b_indicator = (IndicatorBit[hintIndex/8] >> (hintIndex % 8)) & 1;
		if (ExtraPart[hintIndex] == queryPartNum && ExtraOffset[hintIndex] == queryOffset)
			break;
		uint32_t r = prf.PRF4Idx(HintID[hintIndex], queryPartNum);	
		if ((r ^ query) & (PartSize-1))	// Check if r == query mod PartSize
			continue;
		bool b = prf.PRF4Select(HintID[hintIndex], queryPartNum, SelectCutoff[hintIndex]);	
		if (b == b_indicator)
			break;
	}
	assert(hintIndex < M);

	// Build a query. Randomize the selector bit that is sent to the server.
	uint32_t hintID = HintID[hintIndex];
	uint16_t prfIndices [8];
	uint32_t prfSelectVals[4];
	bool shouldFlip = rand() & 1;
	uint32_t cutoff = SelectCutoff[hintIndex];
	for (uint32_t k = 0; k < PartNum; k++)
	{
		// Each prf evaluation generates the in-partition offsets for 8 consecutive partitions
		if (k % 8 == 0){
			prf.evaluate((uint8_t*)prfIndices, hintID, k / 8, 2);
		}
		// Each prf evaluation generates the v values for 4 consecutive partitions
		if (k % 4 == 0){
			prf.evaluate((uint8_t *)prfSelectVals, hintID, k / 4, 1);
		}

		if (k == queryPartNum)	// current partition is the partition of interest
		{
			bvec[k] = (!b_indicator) ^ shouldFlip;	// dummy
			Svec[k] = NextDummyIdx() & (PartSize-1);
			continue; 
		}
		else if (ExtraPart[hintIndex] == k) // current partition is the hint's extra partition
		{
			bvec[k] = b_indicator ^ shouldFlip;	// real
			Svec[k] = ExtraOffset[hintIndex];
			continue;
		}	

		bool b = prfSelectVals[k % 4] < cutoff;
		bvec[k] = b ^ shouldFlip;
		if (b == b_indicator)
			Svec[k] = prfIndices[k % 8] & (PartSize-1);
		else
			Svec[k] = NextDummyIdx() & (PartSize-1);	
	}

	// Make our query
	memset(Response_b0, 0, sizeof(uint64_t) * B);
	memset(Response_b1, 0, sizeof(uint64_t) * B);
	online_server.onlineQuery(bvec, Svec, Response_b0, Response_b1);
	
	// Set the query result to the correct response.
	uint64_t * QueryResult = Response_b1;
	if ((!b_indicator) ^ shouldFlip){
		QueryResult = Response_b0;
	} 
	for (uint32_t l = 0; l < B; l++)
		result[l] = QueryResult[l] ^ Parity[hintIndex*B + l]; 


	#ifdef DEBUG
	// Check actual value 
	online_server.getEntryFromServer(query, tmpEntry);

	for (uint32_t l = 0; l < B; l++){
		if (result[l] != tmpEntry[l]) {
			cout << "Query failed, debugging query " << query << endl;
			assert(query < N);
			cout << "Computed result: " << result[l] << "\n" << "Actual entry value: " << tmpEntry[l] << endl;
			cout << "Parity sent by server: " << QueryResult[l] << "\nHint parity: " << Parity[hintIndex*B + l] << endl;
			for (uint32_t k = 0; k < PartNum; k++)
			{
				if (query / PartSize != k && ExtraPart[hintIndex] != k)
				{
					assert(prf.PRF4Select(hintID, k, cutoff) == bvec[k] ^ shouldFlip);
					assert(!(bvec[k] ^ shouldFlip) || prf.PRF4Idx(hintID, k) % PartSize == Svec[k]);
				}
			}
			cout << "Extra entry partition num: " << ExtraPart[hintIndex] << "\nExtra entry offset: " << ExtraOffset[hintIndex] << endl;
			assert(0);
		}
	}
	#endif

	// Run client side part of Algorithm 3.
  // Replenish hint. Parity indicator represents the bit that we will use for our hint.
	uint64_t hint_parities[2*B];
	++LastHintID;
	offline_server.replenishHint(LastHintID, hint_parities, SelectCutoff + hintIndex);

	b_indicator = !(prf.PRF4Select(LastHintID, queryPartNum, SelectCutoff[hintIndex]));
	IndicatorBit[hintIndex/8] = (IndicatorBit[hintIndex/8] & ~(1<<(hintIndex%8))) | b_indicator << (hintIndex % 8);
	HintID[hintIndex] = LastHintID;
	ExtraPart[hintIndex] = queryPartNum;
	ExtraOffset[hintIndex] = queryOffset; 
	for (uint32_t l = 0; l < B; l++){
		Parity[hintIndex*B+l] = hint_parities[b_indicator*B+l] ^ result[l];
	}
}

OneSVClient::OneSVClient(uint32_t LogN, uint32_t EntryB):
	prf(AES_KEY)
{
	assert(LogN < 32);
	assert(EntryB >= 8);
	N = 1 << LogN;
	B = EntryB / 8;
	EntrySize = EntryB;

	PartNum = 1 << (LogN / 2);
	PartSize = 1 << (LogN / 2 + LogN % 2);
	lambda = LAMBDA;
	M = lambda * PartSize;

	// Allocate storage for hints
	HintID = new uint32_t [M];
	SelectCutoff = new uint32_t [M*2];	
	Parity = new uint64_t [M*2*B];
	ExtraPart = new uint16_t [M];
	ExtraOffset = new uint16_t [M];
	FlipCutoff = new bool [M];	

	prfSelectVals = new uint32_t [PartNum*4];	// temporary for offline online
	DBPart = new uint64_t [PartSize * B]; // streamed partition
	dummyIdxUsed = 0;			// ever increasing to not repeat, use % 8 to index

	// request to server and response from server
	bvec = new bool [PartNum];
	Svec = new uint32_t [PartNum];
	Response_b0 = new uint64_t [B];
	Response_b1 = new uint64_t [B];
	tmpEntry = new uint64_t [B];
}


void OneSVClient::Offline(OneSVServer &server) {
	Q = 0;
	BackupUsedAgain = 0;
	memset(Parity, 0, sizeof(uint64_t) * B * M * 2);
	memset(FlipCutoff, 0, sizeof(bool)*M);
	
	uint32_t InvalidHints = 0;
	uint32_t prfOut [4]; 
	for (uint32_t j = 0; j < M + M/2; j++)
	{
		if ((j % 4) == 0)
		{
			for (uint32_t k = 0; k < PartNum; k++)
			{
				prf.evaluate((uint8_t*) prfOut, j / 4, k, 1);
				for (uint8_t l = 0; l < 4; l++)
					prfSelectVals[PartNum*l+ k] = prfOut[l];
			}
		}
		SelectCutoff[j] = FindCutoff(prfSelectVals + PartNum*(j%4), PartNum);
		InvalidHints += !SelectCutoff[j];	
	}
	cout << "Offline: cutoffs done, invalid hints: " << InvalidHints << endl;

	uint16_t prfIndices [8];
	for (uint32_t j = 0; j < M; j++)
	{
		HintID[j] = j;

		uint16_t ePart;
		bool b = 1;
		while (b)	// keep picking until hitting an un-selected partition
		{
			ePart = NextDummyIdx() % PartNum; 
			b = prf.PRF4Select(j, ePart, SelectCutoff[j]);	
		}
		uint16_t eIdx = NextDummyIdx() % PartSize;
		ExtraPart[j] = ePart;
		ExtraOffset[j] = eIdx;
	}
	cout << "Offline: extra indices done." << endl;

  // Run Algorithm 4
  // Simulates streaming the entire database one partition at a time.
	uint64_t *DBPart = new uint64_t [PartSize * B]; 
	for (uint32_t k = 0; k < PartNum; k++)
	{
		for (uint32_t i = 0; i < PartSize; i++)
			server.getEntry(k*PartSize + i, DBPart + i * B);
	
		for (uint32_t j = 0; j < M + M/2; j++)
		{
			if ((j % 4) == 0)
				prf.evaluate((uint8_t*) prfOut, j / 4, k, 1);
			if ((j % 8) == 0)
				prf.evaluate((uint8_t*) prfIndices, j / 8, k, 2);
			bool b = prfOut[j % 4] < SelectCutoff[j];
			uint16_t r = prfIndices[j % 8] & (PartSize-1);	// faster than mod 
				
			if (j < M)
			{
				if (b)
					for (uint32_t l = 0; l < B; l++)
						Parity[j*B+l] ^= DBPart[r*B+l];
				else if (ExtraPart[j] == k) 
					for (uint32_t l = 0; l < B; l++)
						Parity[j*B+l] ^= DBPart[ExtraOffset[j] * B + l];
			}
			else			// construct backup hints in pairs
			{
				uint32_t dst = j * B + (!b) * B * M/2;
				for (uint32_t l = 0; l < B; l++)
					Parity[dst+l] ^= DBPart[r*B+l];
			}
		}
	}
}
	


uint16_t OneSVClient::NextDummyIdx()
{
	if (dummyIdxUsed % 8 == 0)	// need more dummy indices
		prf.evaluate((uint8_t*) prfDummyIndices, 0, dummyIdxUsed / 8, 0);
	return prfDummyIndices[dummyIdxUsed++ % 8]; 
}

void OneSVClient::Online(OneSVServer &server, uint32_t query, uint64_t *result)
{
	if (query >= N)	query -= N;
	uint16_t queryPartNum = query / PartSize;
	uint16_t queryOffset = query & (PartSize-1);
	uint32_t hintIndex = 0;
	
	// Run Algorithm 2
	// Find a hint that has our desired q
	// checking ej first won't improveuery index
	for (; hintIndex < M; hintIndex++)		 {
		if (SelectCutoff[hintIndex] == 0) // Invalid hint
			continue;
		if (ExtraPart[hintIndex] == queryPartNum && ExtraOffset[hintIndex] == queryOffset) // Query is the extra entry that the hint stores
			break;
		uint32_t r = prf.PRF4Idx(HintID[hintIndex], queryPartNum);	
		if ((r ^ query) & (PartSize-1))	// Check if r == query mod PartSize
			continue;
		bool b = prf.PRF4Select(HintID[hintIndex], queryPartNum, SelectCutoff[hintIndex], FlipCutoff[hintIndex]);	
		if (b)
			break;
	}
	assert(hintIndex < M);

	// Build a query. Randomize the selector bit that is sent to the server.
	uint32_t hintID = HintID[hintIndex];
	bool shouldFlip = rand() & 1;
	if (hintID > M)
		BackupUsedAgain++;

	for (uint32_t k = 0; k < PartNum; k++)
	{
		if (k == queryPartNum)	// partition of interest
		{
			bvec[k] = 0 ^ shouldFlip;	// dummy
			Svec[k] = NextDummyIdx() & (PartSize-1);
			continue; 
		}
		else if (ExtraPart[hintIndex] == k)
		{
			bvec[k] = 1 ^ shouldFlip;	// real
			Svec[k] = ExtraOffset[hintIndex];
			continue;
		}	

		bool b = prf.PRF4Select(hintID, k, SelectCutoff[hintIndex], FlipCutoff[hintIndex]);
		bvec[k] =  b ^ shouldFlip;
		if (b)
			Svec[k] = prf.PRF4Idx(hintID, k) & (PartSize-1);
		else
			Svec[k] = NextDummyIdx() & (PartSize-1);	
	}

 // Make our query
	memset(Response_b0, 0, sizeof(uint64_t) * B);
	memset(Response_b1, 0, sizeof(uint64_t) * B);
	server.onlineQuery(bvec, Svec, Response_b0, Response_b1);

	uint64_t * QueryResult = shouldFlip ? Response_b0 : Response_b1;
	 
	for (uint32_t l = 0; l < B; l++)
		result[l] = QueryResult[l] ^ Parity[hintIndex*B + l]; 


#ifdef DEBUG
	// Check for correctness 
	server.getEntry(query, tmpEntry);

	for (uint32_t l = 0; l < B; l++){
		if (result[l] != tmpEntry[l]) {
			cout << "Query failed, debugging query " << query << endl;
			assert(query < N);
			cout << "Computed result: " << result[l] << "\n" << "Actual entry value: " << tmpEntry[l] << endl;
			cout << "Parity sent by server: " << QueryResult[l] << "\nHint parity: " << Parity[hintIndex*B + l] << endl;
			cout << "Extra entry partition num: " << ExtraPart[hintIndex] << "\nExtra entry offset: " << ExtraOffset[hintIndex] << endl;

			assert(0);
		}
	}
#endif

	while (SelectCutoff[M+Q] == 0)	// skip invalid hints
		Q++;

  // Run Algorithm 5
  // Replenish a hint using a backup hint.
	HintID[hintIndex] = M + Q;
	SelectCutoff[hintIndex] = SelectCutoff[M+Q];
	ExtraPart[hintIndex] = queryPartNum;
	ExtraOffset[hintIndex] = queryOffset; 
	FlipCutoff[hintIndex] = prf.PRF4Select(M + Q, queryPartNum, SelectCutoff[M+Q]);		
	uint32_t src = M*B + Q*B + FlipCutoff[hintIndex] * B * M/2;
	for (uint32_t l = 0; l < B; l++)
		Parity[hintIndex*B+l] = Parity[src+l] ^ result[l];
	Q++;
	assert(Q < M/2);
}