
#include "exception.h"
#include <iostream>
#include "qfs_file_system_helper.h"
#include "qfs_file_helper.h"
#include <fcntl.h>
#include <cstring>
//
 using namespace std;
//
int main() {
  QFSHelper *qfs = new QFSHelper();
  qfs->Connect();
  string new_file_name;
  cout << endl << "enter a file name to write data : ";
  cin >> new_file_name;

  QFSFileHelper *qfsfh = new QFSFileHelper(qfs, new_file_name, O_WRONLY);
  if(qfs->IsFileExists(new_file_name)) {
   cout << endl << "File exists";
   qfsfh->Open();
   string data;
   cout << endl << "enter some data to write ";
   cin >> data;

   char *cstr = new char [data.size()+1];
   strcpy (cstr, data.c_str());
   int wrote = qfsfh->Write(cstr, strlen(cstr));

   cout << endl << "wrote " << wrote << " bytes";
   qfsfh->Close();
   cout << endl << "data writtern and file closed";
  } 
  else {
   cout << endl << "File doesnt exists";
  }
  cout << endl;
  qfs->DisConnect();
}
//
// src/test/test_qfsconnect.cpp
 // QFSFileHelper *qfsfh = new QFSFileHelper(qfs, "new_file", O_WRONLY);
 // qfsfh->Create();
 // cout << "Testing QFS host connection";
 // return 0;
