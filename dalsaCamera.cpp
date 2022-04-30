/* 
	Represents a connection to a dalsa camera. Serves as a convenience wrapper around the GigE-V Framework 
*/

#ifndef __DALSACAMERA_CPP__
#define __DALSACAMERA_CPP__

#include <stdlib.h>
#include <stdio.h>
#include <string> 
#include <iostream>
#include <fstream>
#include <math.h>
#include <map>
#include <ctime>
#include <time.h>

using namespace std;

#include <chrono>
using namespace std::chrono;
 
#include <opencv2/opencv.hpp>
using namespace cv;

// GEV lib definitions usually found in /usr/dalsa/GigeV/include
#include "gevapi.h"				

#include "dalsaCamera.h"
#include "videoIO/VideoIO.h"
#include "encoder.cpp"
 
#define NUM_BUF 64 // image buffers
#define TIMEOUT_US 10000 // timeout during image acuqisition

// These settings were taken from genicam_cpp_demo in the GigE-V network. Some tuning could probably be done here
#define HEARTBEAT_TIMEOUT_MS 90000
#define STREAMFRAME_TIMEOUT_MS 1001 			// Internal timeout for frame reception.
#define STREAMFRAME_NUM_FRAMES_BUFFERED 4 			// Buffer frames internally.
#define STREAMFRAME_MEMORY_LIMIT_MAX 64*1024*1024	// Adjust packet memory buffering limit.	
#define STREAMFRAME_PACKET_SIZE 9180					// Adjust the GVSP packet size.
#define STREAMFRAME_PACKET_DELAY 10					// Add usecs between packets to pace arrival at NIC.


DalsaCamera::DalsaCamera(bool debugMode=true)
{
	numBuf = NUM_BUF;
	bufAddress = new PUINT8[numBuf];

	frameCount = 0;
	timestampPrevFrame = -1;
	debug = debugMode;

	_width = 0;
	_height = 0;
	_framerate = 0;
}

int DalsaCamera::width()
{
	return _width;
}

int DalsaCamera::height()
{
	return _height;
}

float DalsaCamera::framerate()
{
	return _framerate;
}

int DalsaCamera::isOpened()
{
	return _isOpened;
}

/* Initialise the camera for frame acquisition  */
int DalsaCamera::open(int width, int height, float framerate, float exposureTime)
{
	// Check validity of framerate and exposure
	if(framerate <= 0)
	{
		cerr << "Invalid Framerate: " << framerate << endl;
		return 1;
	}

	float max_exposure = 1000000/framerate;
	if(max_exposure <= exposureTime)
	{
		cerr << "Exposure longer than framerate (max " << (int)max_exposure << "us" << " for a framerate of " <<  framerate << ")" << endl;
		return 1;
	}

	// Set default options for the library.
	GEVLIB_CONFIG_OPTIONS options = {0};
	GevGetLibraryConfigOptions( &options);
	options.logLevel = GEV_LOG_LEVEL_NORMAL;
	GevSetLibraryConfigOptions(&options);

	// Discover Cameras
	int numCameras = 0;
	//TODO: why does genicam_cpp_demo use the equation (MAX_NETIF * MAX_CAMERAS_PER_NETIF) with the hardcoded value below?
	int maxCameras = 8 * 32;

	GEV_DEVICE_INTERFACE  pCamera[maxCameras] = {0};
	if(GevGetCameraList(pCamera, maxCameras, &numCameras))
	{
		cerr << "Failed to get camera list\n";
		return 1;
	}

	if(!numCameras)
	{
		cerr << "ERROR: Could not find any cameras\n";
		return 1;
	}

	// Open first camera
	// TODO: Handle multiple cmaeras
	int camIndex = 0;
	if(GevOpenCamera(&pCamera[0], GevExclusiveMode, &handle))
	{
		cerr << "Failed to open camera\n";
		return 1;
	}

	// Settings taken from genicam_cpp_demo from GigE-V framework
	// TODO: offload to settings file?
	// TODO: does it work reliably without these settings?
	GEV_CAMERA_OPTIONS camOptions = {0};
	GevGetCameraInterfaceOptions(handle, &camOptions);

	// Set camera stream options (taken from genicam_cpp_demo)
	GevGetCameraInterfaceOptions( handle, &camOptions);
	
	camOptions.heartbeat_timeout_ms = HEARTBEAT_TIMEOUT_MS;					// For debugging (delay camera timeout while in debugger)
	camOptions.streamFrame_timeout_ms = STREAMFRAME_TIMEOUT_MS;				// Internal timeout for frame reception.
	camOptions.streamNumFramesBuffered = STREAMFRAME_NUM_FRAMES_BUFFERED;	// Buffer frames internally.
	camOptions.streamMemoryLimitMax = STREAMFRAME_MEMORY_LIMIT_MAX;			// Adjust packet memory buffering limit.	
	camOptions.streamPktSize = STREAMFRAME_PACKET_SIZE;						// Adjust the GVSP packet size.
	camOptions.streamPktDelay = STREAMFRAME_PACKET_DELAY;					// Add usecs between packets to pace arrival at NIC.

	GevSetCameraInterfaceOptions(handle, &camOptions);

	// Initiliaze access to GenICam features via Camera XML File
	if (GevInitGenICamXMLFeatures(handle, TRUE))
	{
		cerr << "Failed to find xml file for camera \n";
	}

	// Get the name of XML file name back (example only - in case you need it somewhere).
	char xmlFileName[MAX_PATH] = {0};
	if(GevGetGenICamXML_FileName(handle, (int)sizeof(xmlFileName), xmlFileName)) 
	{
		cerr << "Failed to open xml file for camera status: %s\n";
		cerr << "For File: " << xmlFileName;
	}
	
	// Always disable pesky auto-brightness
	int autoBrightness = 0;
	if(GevSetFeatureValue(handle, "autoBrightnessMode", sizeof(autoBrightness), &autoBrightness))
	{
		cerr << "Failed to set autobrightness to " << autoBrightness << endl;
		return 1;
	}

	if(GevSetFeatureValue(handle, "ExposureTime", sizeof(exposureTime), &exposureTime))
	{
		cerr << "Failed to set exposureTime to " << exposureTime << endl;
		return 1;
	}

	if(GevSetFeatureValue(handle, "AcquisitionFrameRate", sizeof(framerate), &framerate))
	{
		cerr << "Failed to set framerate to " << framerate << stat << endl;
		return 1;
	}

	int zero= 0;
	if(GevSetFeatureValue(handle, "OffsetY", sizeof(zero), &zero))
	{
		cerr << "Failed to initialise height offset to " << zero << endl;
		return 1;
	}

	if(GevSetFeatureValue(handle, "OffsetX", sizeof(zero), &zero))
	{
		cerr << "Failed to initialise width offset to " << zero << endl;
		return 1;
	}

	// Whacking down the resolution Setting Feature Values
	if(GevSetFeatureValue(handle, "Width", sizeof(width), &width))
	{
		cerr << "Failed to set width to " << width << endl;
		return 1;
	}
	
	if(GevSetFeatureValue(handle, "Height", sizeof(height), &height))
	{
		cerr << "Failed to set height to " << height << endl;
		return 1;
	}

	// Get camera settings
	// TODO: Assert the retrieved value matches the one passed in
	int type;	
	float readExposed = -1;

	GevGetFeatureValue(handle, "Width", &type, sizeof(width), &width);
	GevGetFeatureValue(handle, "Height", &type, sizeof(height), &height);
	GevGetFeatureValue(handle, "AcquisitionFrameRate", &type, sizeof(framerate), &framerate);
	GevGetFeatureValue(handle, "ExposureTime", &type, sizeof(readExposed), &readExposed);

	// Set height and width offsets to centralise image
	int heightOffset, widthOffset, heightMax, widthMax;	 
	GevGetFeatureValue(handle, "WidthMax", &type, sizeof(widthMax), &widthMax);
	GevGetFeatureValue(handle, "HeightMax", &type, sizeof(heightMax), &heightMax);

	heightOffset= floor((heightMax-height)/2);
	widthOffset= floor((widthMax-width)/2);

	if(GevSetFeatureValue(handle, "OffsetY", sizeof(heightOffset), &heightOffset))
	{
		cerr << "Failed to initialise height offset to " << heightOffset << endl;
		return 1;
	}

	if(GevSetFeatureValue(handle, "OffsetX", sizeof(widthOffset), &widthOffset))
	{
		cerr << "Failed to initialise width offset to " << widthOffset << endl;
		return 1;
	}

	_width = width;
	_height = height;
	_framerate = framerate;
	_exposure = readExposed;

	// Log
	logCamera();

	// Allocate buffers
	UINT32 format=0;
	GevGetFeatureValue(handle, "PixelFormat", &type, sizeof(format), &format);
	int size = height * width * GetPixelSizeInBytes(format);
	int numBuffers = numBuf;
	for(int i = 0; i < numBuffers; i++)
	{
		bufAddress[i] = (PUINT8)malloc(size);
		memset(bufAddress[i], 0, size);
	}

	// Initialise Image Transfer
	// TODO: Use Sync! It's more thread safe
	if(GevInitializeTransfer(handle, Asynchronous, size, numBuf, bufAddress))
	{
		cerr << "Failed to Initiliaze image transfer\n";
		return 1;
	}

	// TODO: Offload this to record/start functions?
	if(GevStartTransfer(handle, -1))
	{
		cerr << "Failed to start image transfer\n";
		return 1;
	}	

	// Obtain the first image so _tNextFrameMicroseconds can be set
	GEV_BUFFER_OBJECT* img_obj = nextAcquiredImage();
	_tNextFrameMicroseconds = periodMicroseconds() + 
		combineTimestamps(img_obj->timestamp_lo, img_obj->timestamp_hi);

	_isOpened = 1;

	// For framerate acquisition logging
	tStart = time(NULL);

	return 0;
}

/* Log camera information */
// TODO: Use boost logging framework
void DalsaCamera::logCamera()
{
	if(handle == NULL || !debug)
	{
		return;
	}

	printf("Camera Settings: \n");
	printf("\tWidth: %i\n", _width);
	printf("\tHeight: %i\n", _height);
	printf("\tFramerate: %.1f\n", _framerate);
	printf("\texposureTime (us): %f\n", _exposure);

	int type;	
	char value_str[MAX_PATH] = {0};
	GevGetFeatureValueAsString(handle, "PixelFormat", &type, MAX_PATH, value_str);
	printf("\tPixelFormat (str) = %s\n", value_str);
}

/* Obtains next image transfered over UDP */
GEV_BUFFER_OBJECT* DalsaCamera::nextAcquiredImage()
{
	GEV_BUFFER_OBJECT* imgGev = NULL;
	int status;
	
	status = GevWaitForNextImage(handle, &imgGev, TIMEOUT_US);

	// Check that we have received data ok, 
	// TODO: Nicer to put in next_acquired_image
	if(imgGev == NULL)
	{
		cerr << "Failed to wait for next acquired image.\n";
		cerr << "NULL Image Object.\n";
		cerr << "possibly caused by filling up GigE-V buffers\n";
		throw "next_acquired_image failure";
	}
	if(status != GEVLIB_OK)
	{
		cerr << "Failed to wait for next acquired image.\n";
		cerr << "GevWaitForNextImage returned " << status << "\n";
		throw "next_acquired_image failure";
	}
	if(imgGev->status != 0)
	{
		cerr << "Failed to wait for next acquired image.\n";
		cerr << "img->status = " << imgGev->status << "\n";
		cerr << "Could be a bandwidth problem\n";
		// throw "next_acquired_image failure";
	}
	// Check image data is actually there
	if(imgGev -> address == NULL)
	{
		cerr << "Failed to wait for next acquired image.\n";
		cerr << "img->address = NULL\n";
		throw "next_acquired_image failure";
	}

	return imgGev;
}

/* Get next image. 
*	Returns debayered images in the order they were acquired by the camera.
*	This works by caching frames temporarily in a timestamp map.
*/
int DalsaCamera::getNextImage(cv::Mat *img)
{
	// Check for camera state
	if(!isOpened())
	{
		cerr << "open camera before calling get_next_image";
		return 1;
	}

	// Cache frames to the map until the next one is acquired
	uint64_t next_timestamp = 0;
	while(!next_timestamp)
	{
		// Acquire next image and cache into map
		GEV_BUFFER_OBJECT *nextImage = nextAcquiredImage();
		uint64_t acquired_t = combineTimestamps(nextImage->timestamp_lo, nextImage->timestamp_hi);
		_reorderingMap[acquired_t] = nextImage;

		// Check for _tNextFrameMicroseconds within a microsecond tolerance to account for rounding error
		for(uint64_t t=_tNextFrameMicroseconds-2; t<=_tNextFrameMicroseconds+2; t++)
		{
			if(_reorderingMap.find(t) != _reorderingMap.end())
			{
				next_timestamp = t;
				break;
			}
		}
	}
	
  	// Get the next frame
   	GEV_BUFFER_OBJECT *imgGev = _reorderingMap[next_timestamp];
   	_reorderingMap.erase(next_timestamp);

	logImg(imgGev);

	//TODO: handle a reset of next frame
	_tNextFrameMicroseconds = next_timestamp + periodMicroseconds();

	// Debayer the image
    cv::Mat imgCv = cv::Mat(height(), width(), CV_8UC1, imgGev->address);
    cv::Mat rgb8BitMat(height(), width(), CV_8UC3);
    cv::cvtColor(imgCv, rgb8BitMat, CV_BayerGB2RGB);

    *img = rgb8BitMat;

    // Release Image Buffer
	GevReleaseImage(handle, imgGev);
	imgCv.release();

	return 0;
}

/* Log an Image */
// TODO: Use boost logging framework
void DalsaCamera::logImg(GEV_BUFFER_OBJECT *imgGev)
{
	// Update Counter
	frameCount++;

	if(!debug)
	{
		return;
	}

	printf("Acquired Image:\n");
	printf("\tTimestamp hi: %u\n", imgGev->timestamp_hi);
	printf("\tTimestamp low: %u\n", imgGev->timestamp_lo);
	printf("\tw: %i\n", imgGev->w);
	printf("\th: %i\n", imgGev->h);
	printf("\td: %i\n", imgGev->d);
	printf("\tformat: %i\n", imgGev->format);
	printf("\taddress: %x\n", *imgGev->address);
	printf("\timg_gev->status: %i\n", imgGev->status);

	time_t tEnd = time(NULL);
	float avgFramerate = (float)frameCount / ((float) ((long)tEnd-(long)tStart));
	printf("\tAvg Framerate: %.0f\n", avgFramerate);
	printf("\n");
}

/* Record some video */
int DalsaCamera::record(float duration, int crf, char filename[])
{
	int numFrames = round(duration * float(_framerate));

	// Initialise video writer
	Encoder writer(filename, (int)(width()), (int)(height()), _framerate, crf, debug);

    // Collect the frames
	for(int i=0; i<numFrames; i++)
	{
		cout << "\rElasped: " 
			<< (int) (((float)i+1)/_framerate) 
			<< "s of " 
			<< ceil(((float)numFrames)/_framerate) 
			<< "s"
			<< std::flush;

		cv::Mat img;
		getNextImage(&img);

		// Write the current frame to the mp4 file
		if (writer.writeFrame(img))
		{
			fprintf( stderr, "Could not write frame\n" );
			return -2;
		}
	}

	writer.close();
}

/* Save next image to file */
int DalsaCamera::snapshot(char filename[])
{
	cv::Mat img;
	getNextImage(&img);

	cv::imwrite(filename, img);
}

/* Period in Microseconds */ 
int DalsaCamera::periodMicroseconds()
{
	return round(1.0/_framerate*1000000.0);
}

/* Hoursekeeping */ 
int DalsaCamera::close() 
{
	printf("Closing camera...\n");

	UINT16 status;

	// Must close everything in order, otherwise things may hang. 
	// (1) Camera
	// (2) Gev API
	// (3) Sockets

	// TODO: Although this works, try/catch could be avoided by checking _isOpened etc....
	try
	{
		GevAbortTransfer(handle);
		status = GevFreeTransfer(handle);	
	}
	catch(...){}

	try
	{
		GevCloseCamera(&handle);
	}
	catch(...){}

	// Close down the API.
	try
	{
		GevApiUninitialize();
	}
	catch(...){}

	// Close socket API
	try
	{
		_CloseSocketAPI();	
	}
	catch(...){}

	//TODO: Deallocate buffers

	_isOpened = 0;
	return status;
}

/* combine high and low timestamps into 64 bit int */
uint64_t DalsaCamera::combineTimestamps(uint32_t low, uint32_t high)
{
    return ((uint64_t)high << 32) + low;
}

#endif /* __DALSACAMERA_CPP__ */
