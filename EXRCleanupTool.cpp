////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2020, Dmitrii David Ginzburg.  All Rights Reserved.
//
////////////////////////////////////////////////////////////////////

#include <iostream>
#include <ctime>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h> 
#include <algorithm>
#include <ImfRgbaFile.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStringAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfArray.h>
#include <half.h>
#include <float.h>
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <ImfConvert.h>
#include <math.h>
#include <zlib.h>
#include <ImfRgbaFile.h>
#include <ImfFrameBuffer.h>
#include <ImfChannelList.h>
#include <ImfArray.h>
#include <ImfThreading.h>
#include <IlmThread.h>
#include <Iex.h>
#include <iostream>
#include <cassert>
#include <stdio.h>
#include <vector>
#include <stdexcept>
#include <pthread.h>
#include <windows.h>
#include <tchar.h>

HALF_EXPORT const half::uif half::_toFloat[1 << 16] =
    #include "toFloat.h"
HALF_EXPORT const unsigned short half::_eLut[1 << 9] =
    #include "eLut.h"

#define NUM_THREADS     100
using namespace OPENEXR_IMF_NAMESPACE;
using namespace std;
using namespace IMATH_NAMESPACE;

typedef struct RgbaF
{
	float r;
	float g;
	float b;
	float a;
};

Array2D<RgbaF> pix_res (0,0);
vector<string> dirList;
vector<string> fileList;

bool initializeOptions (int argc, char* argv[])
{
	return true;
}

// Write image
void writeRgba (const char fileName[],
				const Rgba *pixels,
				int width,
				int height)
{
	RgbaOutputFile file (fileName, width, height, WRITE_RGBA);
	file.setFrameBuffer (pixels,  1, width);
	file.writePixels (height);
}

// Read image
void readImage (const char fileName[],
				Array2D<Rgba> &pixels,
				int &width,
				int &height)
{
	RgbaInputFile file (fileName);
	Box2i dw = file.dataWindow();

	width  = dw.max.x - dw.min.x + 1;
	height = dw.max.y - dw.min.y + 1;
	pixels.resizeErase (height, width);

	file.setFrameBuffer (&pixels[0][0] - dw.min.x - dw.min.y * width, 1, width);
	file.readPixels (dw.min.y, dw.max.y);
}

bool headerInfo(const char fileName[])
{
	InputFile file (fileName);
	set<string> layerNames;
	file.header().channels().layers (layerNames);
	bool isStandard = true;
	for (set<string>::const_iterator i = layerNames.begin();i != layerNames.end();++i)
		if((*i == "diffuse_mse")||(*i == "specular_mse")||(*i == "normal_var")){
			isStandard = false;
		}

	return isStandard;
}

void writeImage32bit(const char fileName[],
					Array2D<RgbaF> &pixels,
					int &width,
					int &height,
					Box2i &dataWindow)
{
	width  = dataWindow.max.x - dataWindow.min.x + 1;
	height = dataWindow.max.y - dataWindow.min.y + 1;
	int dx = dataWindow.min.x;
	int dy = dataWindow.min.y;

	Header header (width, height);
	header.dataWindow() = dataWindow;
	string commentsStr = "Created using: (c)D.Ginzburg EXR Cleanup Tool 1.1";
	header.insert ("comments",  StringAttribute(commentsStr));
	header.channels().insert ("R", Channel (Imf::FLOAT));
	header.channels().insert ("G", Channel (Imf::FLOAT));
	header.channels().insert ("B", Channel (Imf::FLOAT));
	header.channels().insert ("A", Channel (Imf::FLOAT));
	OutputFile file (fileName, header);

	FrameBuffer frameBuffer;
	frameBuffer.insert ("R",								// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].r,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	
	frameBuffer.insert ("G",								// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].g,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride

	frameBuffer.insert ("B",								// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].b,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride

	frameBuffer.insert ("A",								// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].a,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	file.setFrameBuffer (frameBuffer);
	file.writePixels (dataWindow.max.y - dataWindow.min.y + 1);
}

void readImage32bit (const char fileName[],
					Array2D<RgbaF> &pixels,
					int &width,
					int &height,
					Box2i &dataWindow)
{
	InputFile file (fileName);
	Box2i dw = file.header().dataWindow();
	dataWindow = dw;
	width  = dw.max.x - dw.min.x + 1;
	height = dw.max.y - dw.min.y + 1;
	int dx = dw.min.x;
	int dy = dw.min.y;
	pixels.resizeErase (height, width);

	string xR = "R";
	string xG = "G";
	string xB = "B";
	string xA = "A";
	FrameBuffer frameBuffer;
	frameBuffer.insert (xR,									// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].r,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	
	frameBuffer.insert (xG,									// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].g,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	
	frameBuffer.insert (xB,									// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].b,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	
	frameBuffer.insert (xA,									// name
						Slice (Imf::FLOAT,					// type
						(char *) &pixels[-dy][-dx].a,		// base
						sizeof (pixels[0][0]) * 1,			// xStride
						sizeof (pixels[0][0]) * width));	// yStride
	
	file.setFrameBuffer (frameBuffer);
	file.readPixels (dw.min.y, dw.max.y);
}

bool CompleteEXRCheck(const char fileName[]){
	InputFile file (fileName);
	bool result = file.isComplete();

	return result;
}

int getPadding(char *name)
{
	int padding = 0;
	std::string fname = name;
	for(int i=0; i< fname.length(); i++){
		if(fname[i] == '#')
			padding++;
	}
	return padding;
}

std::string FillZero(int number, int padding)
{
	std::string snumber;
	std::stringstream out;
	out << number;
	snumber = out.str();
		
	std::string zerofill = "";
	if((snumber.length() < padding) && (number > -1))
		for(int i=0; i < padding - snumber.length(); i++)
			zerofill += "0";
	return zerofill+snumber;
}

std::string FillPads(std::string name,int frame, int padding)
{
	std::string out = name;
	int strIndex = 0;
	for(int i=0; i < out.length(); i++)
		if(out[i] == '#')
		{
			strIndex = i;
			break;
		}
	
	for(int i=0; i < padding; i++)
		out[strIndex+i] = FillZero(frame,padding)[i];
	return out;
}

std::string OutFileName(std::string SourceName,int frame, int padding, char *oPostfix)
{
	std::string out = SourceName;
	int strIndex = 0;
	for(int i=0; i < out.length(); i++)
		if(out[i] == '#')
		{
			strIndex = i;
			break;
		}
	
	for(int i=0; i < padding; i++)
		out[strIndex+i] = FillZero(frame,padding)[i];
	
	std::string pre;
	for(int i=0; i < strIndex-1; i++)
		pre += out[i];
	pre += oPostfix;
	for(int i=strIndex-1; i < out.length(); i++)
		pre += out[i];
	return pre;
}

void FindFiles(string folderName)
{
	string sourcePath_f = folderName+"\\*.*";
	wstring secondPath_f;
	for(int i = 0; i < sourcePath_f.length(); ++i)
		secondPath_f += wchar_t( sourcePath_f[i] );
	
	const wchar_t* ResultPath_f = secondPath_f.c_str();

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;
	
	// Find files
	hFind = FindFirstFile(ResultPath_f , &FindFileData );
	do {
		if( hFind  && !( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY  ) )
		{
			wchar_t* txt = FindFileData.cFileName;
			wstring ws(txt);
			string str(ws.begin(), ws.end());
			size_t exrPos = str.find(".exr");
			size_t variancePos = str.find("_variance.");
			if ((exrPos != std::string::npos)&&(variancePos != std::string::npos))
				fileList.push_back(folderName+"\\"+str);
		}
	} while( FindNextFile( hFind, &FindFileData ) );
}

void FindFolders(string folderName)
{
	string sourcePath = folderName+"\\*";
	wstring secondPath;
	for(int i = 0; i < sourcePath.length(); ++i)
		secondPath += wchar_t( sourcePath[i] );
	
	const wchar_t* ResultPath = secondPath.c_str();

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;

	//Find dir
	hFind = FindFirstFile(ResultPath , &FindFileData );
	do {
		if( hFind  && ( FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY  ) )
		{
			wchar_t* txt = FindFileData.cFileName;
			wstring ws(txt);
			string str(ws.begin(), ws.end());
			if((str != ".")&&(str != ".."))
			{
				dirList.push_back(folderName+"\\"+str);
				FindFolders(folderName+"\\"+str);
			}
		}	
	} while( FindNextFile( hFind, &FindFileData ) );
}

void fileProcess(string folderName)
{
	//Init dir
	dirList.push_back(folderName);
	// find process
	FindFolders(folderName);
	if(dirList.size() != 0)
		for(int i = 0; i < dirList.size(); ++i){
			FindFiles(dirList[i]);
			out << "Folder(" << i+1 << "/" << dirList.size() << "):" << dirList[i] << endl;
			float dcount = 1;
			for(int i = 0; i < fileList.size(); ++i){
				cout << "isComplete " << CompleteEXRCheck(fileList[i].c_str()) << endl;
				if((headerInfo(fileList[i].c_str()) != true)&&(CompleteEXRCheck(fileList[i].c_str())))
				{
					int width, height;
					Box2i dataWindow;
					cout << "    " << fileList[i].c_str() << endl;
					readImage32bit(fileList[i].c_str(),pix_res,width, height,dataWindow);
					string tmp = fileList[i]+".CTtmp";
					writeImage32bit(tmp.c_str(),pix_res,width, height,dataWindow);
					int result = remove(fileList[i].c_str());
					result = rename(tmp.c_str(),fileList[i].c_str());
				}

				if(fileList.size() != 0){
					printf("\rProcess %d%% complete.", ((i+1)*100)/(fileList.size()) );
				}
				else
				{
					printf("\rProcess %d%% complete.", 100 );
				}
			}
			cout << "" << endl;
			fileList.clear();
		}
}

int main(int argc, char* argv[])
{
	cout << "(c)D.Ginzburg EXR Cleanup Tool, 2020 version 1.1" <<  endl;

	string dirPath;
		
	if(argc <= 1){
		cin >> dirPath;
	} else
	{
		dirPath = argv[1];
	}
	fileProcess(dirPath);
	return 0;
}

