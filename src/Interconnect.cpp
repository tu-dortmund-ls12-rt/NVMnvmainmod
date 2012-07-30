/*
 *  This file is part of NVMain- A cycle accurate timing, bit-accurate
 *  energy simulator for non-volatile memory. Originally developed by 
 *  Matt Poremba at the Pennsylvania State University.
 *
 *  Website: http://www.cse.psu.edu/~poremba/nvmain/
 *  Email: mrp5060@psu.edu
 *
 *  ---------------------------------------------------------------------
 *
 *  If you use this software for publishable research, please include 
 *  the original NVMain paper in the citation list and mention the use 
 *  of NVMain.
 *
 */


#include "src/Interconnect.h"
#include "src/MemoryController.h"
#include "include/NVMainRequest.h"


using namespace NVM;


void Interconnect::RequestComplete( NVMainRequest *request )
{
  /*
   *  By default, just tell the issuing controller the request has been completed
   *  as soon as it arrives on the interconnect.
   */
  if( request->issueController != NULL )
    {
      request->issueController->RequestComplete( request );
    }
}


