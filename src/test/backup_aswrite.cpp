#include "store.h"
#include "append_store.h"
#include <iostream>
#include <cstdlib>
#include <time.h>
#include "append_store_types.h"
#include "store.h"
#include "exception.h"
#include <iostream>
#include "qfs_file_system_helper.h"
#include "qfs_file_helper.h"
#include <fcntl.h>
#include <cstring>

using namespace std;

int main() {
	long CHUNK_SIZE = 4 * 1024; // 4 KB
	// long NUM_CHUNKS = 4;
	long FILE_SIZE = CHUNK_SIZE;
	long NUM_FOLDERS = 5;
	long NUM_FILES = 5 * 1024;
	long NUM_FILES_IN_FOLDER = NUM_FILES / NUM_FOLDERS;
	long TOTAL_SIZE = NUM_FILES * FILE_SIZE;
	char DATA[10][CHUNK_SIZE];
	string ROOT_FOLDER = "BACKUPSTORE/AS";
	stringstream stream;
	string new_file_name;
	int fd = -1;
	int i,j,k;

	if(TOTAL_SIZE != (FILE_SIZE * NUM_FOLDERS * NUM_FILES_IN_FOLDER)) {
		cout << endl << "something wrong";
		cout << endl << TOTAL_SIZE;
		cout << endl << FILE_SIZE * NUM_FOLDERS * NUM_FILES_IN_FOLDER;
		return 0;
	}
	
	// Data Generation
	for(i=0; i < 10; i++) {
		for(j=0; j < CHUNK_SIZE; j++) {
			DATA[i][j] = '0' + i;
		}
	}

	// File Write

	StoreParameter sp = StoreParameter(); 
	std::stringstream sstm;
	sstm << ROOT_FOLDER;
	string store_name = sstm.str();
	sp.mPath = store_name;
	sp.mAppend = true;
	PanguAppendStore *pas = new PanguAppendStore(sp, true);

	for(i=0; i < NUM_FOLDERS; i++) {
		for(j=0; j < NUM_FILES_IN_FOLDER; j++) {
			string h1_str = pas->Append(DATA[j % 10]);
			Handle h1(h1_str);
			cout << endl << "(" << h1.mChunkId << ":" << h1.mIndex << ")";			
		}
	}

	pas->Flush();
	pas->Close();

}

		





