/****************************************************************************************************

RepRapFirmware - G Codes

This class interprets G Codes from one or more sources, and calls the functions in Move, Heat etc
that drive the machine to do what the G Codes command.

-----------------------------------------------------------------------------------------------------

Version 0.1

13 February 2013

Adrian Bowyer
RepRap Professional Ltd
http://reprappro.com

Licence: GPL

****************************************************************************************************/

#include "RepRapFirmware.h"

GCodes::GCodes(Platform* p, Webserver* w)
{
  active = false;
  platform = p;
  webserver = w;
  webGCode = new GCodeBuffer(platform, "web: ");
  fileGCode = new GCodeBuffer(platform, "file: ");
}

void GCodes::Exit()
{
   active = false;
}

void GCodes::Init()
{
  webGCode->Init();
  fileGCode->Init();
  webGCodeFinished = true;
  fileGCodeFinished = true;
  active = true;
  moveAvailable = false;
  heatAvailable = false;
  drivesRelative = true;
  axesRelative = false;
  checkEndStops = false;
  gCodeLetters = GCODE_LETTERS;
  distanceScale = 1.0;
  for(int8_t i = 0; i < DRIVES - AXES; i++)
    lastPos[i] = 0.0;
  fileBeingPrinted = -1;
  fileToPrint = -1;
  homeX = false;
  homeY = false;
  homeZ = false;
  homeXQueued = false;
  homeYQueued = false;
  homeZQueued = false;
  dwellWaiting = false;
  dwellTime = platform->Time();
}

void GCodes::Spin()
{
  if(!active)
    return;
    
  if(!webGCodeFinished)
  {
    webGCodeFinished = ActOnGcode(webGCode);
    return;
  }

  if(!fileGCodeFinished)
  {
    fileGCodeFinished = ActOnGcode(fileGCode);
    return;
  }  
    
  if(webserver->GCodeAvailable())
  {
    if(webGCode->Put(webserver->ReadGCode()))
      webGCodeFinished = ActOnGcode(webGCode);
    return;
  }
  
  if(fileBeingPrinted >= 0)
  {
     char b;
     if(platform->Read(fileBeingPrinted, b))
     {
       if(fileGCode->Put(b))
         fileGCodeFinished = ActOnGcode(fileGCode);
     } else
     {
        if(fileGCode->Put('\n')) // In case there wasn't one ending the file
         fileGCodeFinished = ActOnGcode(fileGCode);
        platform->Close(fileBeingPrinted);
        fileBeingPrinted = -1;
     }
  }
}

// Move expects all axis movements to be absolute, and all
// extruder drive moves to be relative.  This function serves that.
// If the Move class can't receive the move (i.e. things have to wait)
// this returns false, otherwise true.

boolean GCodes::SetUpMove(GCodeBuffer *gb)
{
  // Last one gone yet?
  
  if(moveAvailable)
    return false;
    
  // Load the last position; If Move can't accept more, return false
  
  if(!reprap.GetMove()->GetCurrentState(moveBuffer))
    return false;
  
  // What does the G Code say?
  
  for(int8_t i = 0; i < DRIVES; i++)
  {
    if(i < AXES)
    {
      if(gb->Seen(gCodeLetters[i]))
      {
        if(axesRelative)
          moveBuffer[i] += gb->GetFValue()*distanceScale;
        else
          moveBuffer[i] = gb->GetFValue()*distanceScale;
      }
    } else
    {
      if(gb->Seen(gCodeLetters[i]))
      {
        if(drivesRelative)
          moveBuffer[i] = gb->GetFValue()*distanceScale;
        else
          moveBuffer[i] = gb->GetFValue()*distanceScale - lastPos[i - AXES];
      }
    }
  }
  
  // Deal with feedrate
  
  if(gb->Seen(gCodeLetters[DRIVES]))
    moveBuffer[DRIVES] = gb->GetFValue()*distanceScale/60.0; // Feedrates are in mm/minute; we need mm/sec
    
  // Remember for next time if we are switched
  // to absolute drive moves
  
  for(int8_t i = AXES; i < DRIVES; i++)
    lastPos[i - AXES] = moveBuffer[i];
  
  checkEndStops = false;
  
  moveAvailable = true;
  return true; 
}

boolean GCodes::ReadMove(float* m, boolean& ce)
{
    if(!moveAvailable)
      return false; 
    for(int8_t i = 0; i <= DRIVES; i++) // 1 more for F
      m[i] = moveBuffer[i];
    ce = checkEndStops;
    moveAvailable = false;
    checkEndStops = false;
    return true;
}


boolean GCodes::ReadHeat(float* h)
{

}

boolean GCodes::DoHome()
{
  // Treat more or less like any other move
  // Do one axis at a time, starting with X.
  
  // Last one gone yet?
  
  if(moveAvailable)
    return false;
  
  // Wait for all the queued moves to stop
      
  if(!reprap.GetMove()->AllMovesAreFinished())
    return false;
  reprap.GetMove()->ResumeMoving();
    
  // Load the last position; If Move can't accept more, return false - should never happen
  
  if(!reprap.GetMove()->GetCurrentState(moveBuffer))
    return false;

  
  if(homeX)
  {
    if(homeXQueued)
    {
      if(!reprap.GetMove()->AllMovesAreFinished())
        return false;
      reprap.GetMove()->ResumeMoving();
      homeX = false;
      homeXQueued = false;
      return NoHome();
    } else
    {
      moveBuffer[X_AXIS] = -2.0*platform->AxisLength(X_AXIS);
      moveBuffer[DRIVES] = platform->HomeFeedRate(X_AXIS)/60.0;
      homeXQueued = true;
      checkEndStops = true;
      moveAvailable = true; 
      return false;
    }
  }
  
  if(homeY)
  {
    if(homeYQueued)
    {
      if(!reprap.GetMove()->AllMovesAreFinished())
        return false;
      reprap.GetMove()->ResumeMoving();
      homeY = false;
      homeYQueued = false;
      return NoHome();
    } else
    {
      moveBuffer[Y_AXIS] = -2.0*platform->AxisLength(Y_AXIS);
      moveBuffer[DRIVES] = platform->HomeFeedRate(Y_AXIS)/60.0;
      homeYQueued = true;
      checkEndStops = true;
      moveAvailable = true; 
      return false;
    }
  }
  
  if(homeZ)
  {
    if(homeZQueued)
    {
      if(!reprap.GetMove()->AllMovesAreFinished())
        return false;
      reprap.GetMove()->ResumeMoving();
      homeZ = false;
      homeZQueued = false;
      return NoHome();
    } else
    {
      moveBuffer[Z_AXIS] = -2.0*platform->AxisLength(Z_AXIS);
      moveBuffer[DRIVES] = platform->HomeFeedRate(Z_AXIS)/60.0;
      homeZQueued = true;
      checkEndStops = true;
      moveAvailable = true; 
      return false;
    }
  }
  
  // Should never get here
  
  checkEndStops = false;
  moveAvailable = false;

  return true;
}

void GCodes::QueueFileToPrint(char* fileName)
{
  fileToPrint = platform->OpenFile(platform->GetGCodeDir(), fileName, false);
}


// Function to handle dwell delays.  Return true for
// Dwell finished, false otherwise.

boolean GCodes::DoDwell(GCodeBuffer *gb)
{
  unsigned long dwell;
  
  if(gb->Seen('P'))
    dwell = 1000ul*(unsigned long)gb->GetLValue(); // P values are in milliseconds; we need microseconds
  else
    return true;  // No time given - throw it away
      
  // Wait for all the queued moves to stop
      
  if(!reprap.GetMove()->AllMovesAreFinished())
    return false;
      
  // Are we already in a dwell?
      
  if(dwellWaiting)
  {
    if((long)(platform->Time() - dwellTime) >= 0)
    {
      dwellWaiting = false;
      reprap.GetMove()->ResumeMoving();
      return true;
    }
    return false;
  }
      
  // New dwell - set it up
      
  dwellWaiting = true;
  dwellTime = platform->Time() + dwell;
  return false;
}

// If the GCode to act on is completed, this returns true,
// otherwise false.  It is called repeatedly for a given
// GCode until it returns true for that code.

boolean GCodes::ActOnGcode(GCodeBuffer *gb)
{
  int code;
  boolean result = true;
  unsigned long dwell;
  
  if(gb->Seen('G'))
  {
    code = gb->GetIValue();
    switch(code)
    {
    case 0: // There are no rapid moves...
    case 1: // Ordinary move
      result = SetUpMove(gb);
      break;
      
    case 4: // Dwell
      result = DoDwell(gb);
      break;
      
    case 10: // Set offsets
      platform->Message(HOST_MESSAGE, "Set offsets received\n");
      break;
    
    case 20: // Inches (which century are we living in, here?)
      distanceScale = INCH_TO_MM;
      break;
    
    case 21: // mm
      distanceScale = 1.0;
      break;
    
    case 28: // Home
      if(NoHome())
      {
        homeX = gb->Seen(gCodeLetters[X_AXIS]);
        homeY = gb->Seen(gCodeLetters[Y_AXIS]);
        homeZ = gb->Seen(gCodeLetters[Z_AXIS]);
        if(NoHome())
        {
          homeX = true;
          homeY = true;
          homeZ = true;
        }
      }
      result = DoHome();
      break;
      
    case 90: // Absolute coordinates
      drivesRelative = false;
      axesRelative = false;
      break;
      
    case 91: // Relative coordinates
      drivesRelative = true;
      axesRelative = true;
      break;
      
    case 92: // Set position
      platform->Message(HOST_MESSAGE, "Set position received\n");
      break;
      
    default:
      platform->Message(HOST_MESSAGE, "GCodes - invalid G Code: ");
      platform->Message(HOST_MESSAGE, gb->Buffer());
      platform->Message(HOST_MESSAGE, "\n");
    }
    return result;
  }
  
  if(gb->Seen('M'))
  {
    code = gb->GetIValue();
    switch(code)
    {
    case 0: // Stop
    case 1: // Sleep
      platform->Message(HOST_MESSAGE, "Stop/sleep received\n");
      break;
    
    case 18: // Motors off ???
      platform->Message(HOST_MESSAGE, "Motors off received\n");
      break;
      
//    case 23: // Set file to print
//      platform->Message(HOST_MESSAGE, "M code for file selected erroneously received.\n");
//      break;
      
    case 24: // Print selected file
      fileBeingPrinted = fileToPrint;
      fileToPrint = -1;
      break;
      
    case 82:
      drivesRelative = false;
      break;
      
    case 83:
      drivesRelative = true;
      break;
   
    case 106: // Fan on
      platform->Message(HOST_MESSAGE, "Fan on received\n");
      break;
    
    case 107: // Fan off
      platform->Message(HOST_MESSAGE, "Fan off received\n");
      break;
    
    case 116: // Wait for everything
      platform->Message(HOST_MESSAGE, "Wait for all temperatures received\n");
      break;
      
    case 111: // Debug level
      if(gb->Seen('S'))
        reprap.debug(gb->GetIValue());
      break;
    
    case 126: // Valve open
      platform->Message(HOST_MESSAGE, "M126 - valves not yet implemented\n");
      break;
      
    case 127: // Valve closed
      platform->Message(HOST_MESSAGE, "M127 - valves not yet implemented\n");
      break;
      
    case 140: // Set bed temperature
      platform->Message(HOST_MESSAGE, "Set bed temp received\n");
      break;
    
    case 141: // Chamber temperature
      platform->Message(HOST_MESSAGE, "M141 - heated chamber not yet implemented\n");
      break;    
     
    default:
      platform->Message(HOST_MESSAGE, "GCodes - invalid M Code: ");
      platform->Message(HOST_MESSAGE, gb->Buffer());
      platform->Message(HOST_MESSAGE, "\n");
    }
    return result;
  }
  
  if(gb->Seen('T'))
  {
    code = gb->GetIValue();
    boolean ok = false;
    for(int8_t i = AXES; i < DRIVES; i++)
    {
      if(code == i - AXES)
      {
        platform->Message(HOST_MESSAGE, "Tool selection received\n");
        ok = true;
      }
    }

    if(!ok)
    {
      platform->Message(HOST_MESSAGE, "GCodes - invalid T Code: ");
      platform->Message(HOST_MESSAGE, gb->Buffer());
      platform->Message(HOST_MESSAGE, "\n");
    }
    
    return result;
  }
  
  // An empty buffer jumps to here and gets disgarded
  
  return result;
}



//*************************************************************************************

GCodeBuffer::GCodeBuffer(Platform* p, char* id)
{ 
  platform = p;
  identity = id; 
}

void GCodeBuffer::Init()
{
  gcodePointer = 0;
  readPointer = -1;
  inComment = false;   
}

boolean GCodeBuffer::Put(char c)
{
  boolean result = false;
  
  gcodeBuffer[gcodePointer] = c;
  
  if(c == ';')
    inComment = true;
    
  if(c == '\n' || !c)
  {
    gcodeBuffer[gcodePointer] = 0;
    Init();
    if(reprap.debug() && gcodeBuffer[0]) // Don't bother with blank/comment lines
    {
      platform->Message(HOST_MESSAGE, identity);
      platform->Message(HOST_MESSAGE, gcodeBuffer);
      platform->Message(HOST_MESSAGE, "\n"); 
    }
    result = true;
  } else
  {
    if(!inComment)
      gcodePointer++;
  }
  
  if(gcodePointer >= GCODE_LENGTH)
  {
    platform->Message(HOST_MESSAGE, "G Code buffer length overflow.\n");
    gcodePointer = 0;
    gcodeBuffer[0] = 0;
  }
  
  return result;
}   

// Is 'c' in the G Code string?
// Leave the pointer there for a subsequent read.

boolean GCodeBuffer::Seen(char c)
{
  readPointer = 0;
  while(gcodeBuffer[readPointer])
  {
    if(gcodeBuffer[readPointer] == c)
      return true;
    readPointer++;
  }
  readPointer = -1;
  return false;
}

// Get a float after a G Code letter

float GCodeBuffer::GetFValue()
{
  if(readPointer < 0)
  {
     platform->Message(HOST_MESSAGE, "GCodes: Attempt to read a GCode float before a search.\n");
     return 0.0;
  }
  float result = (float)strtod(&gcodeBuffer[readPointer + 1], NULL);
  readPointer = -1;
  return result; 
}


// Get an long after a G Code letter

long GCodeBuffer::GetLValue()
{
  if(readPointer < 0)
  {
    platform->Message(HOST_MESSAGE, "GCodes: Attempt to read a GCode int before a search.\n");
    return 0;
  }
  long result = strtol(&gcodeBuffer[readPointer + 1], NULL, 0);
  readPointer = -1;
  return result;  
}




