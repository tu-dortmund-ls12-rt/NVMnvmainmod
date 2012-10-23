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

#include <iostream>
#include <limits>

#include "src/Bank.h"
#include "src/MemoryController.h"
#include "Endurance/EnduranceModelFactory.h"
#include <signal.h>


using namespace NVM;



#define MAX(a,b) (( a > b ) ? a : b )


Bank::Bank( )
{
  conf = NULL;

  nextActivate = 0;
  nextPrecharge = 0;
  nextRead = 0;
  nextWrite = 0;
  nextPowerDown = 0;
  nextPowerUp = 0;
  nextCommand = CMD_NOP;

  state = BANK_CLOSED;
  lastActivate = 0;
  openRow = 0;

  bankEnergy = 0.0f;
  backgroundEnergy = 0.0f;
  activeEnergy = 0.0f;
  burstEnergy = 0.0f;
  refreshEnergy = 0.0f;

  powerCycles = 0;
  feCycles = 0;
  seCycles = 0;
  dataCycles = 0;
  activeCycles = 0;
  utilization = 0.0f;
  writeCycle = false;
  writeMode = WRITE_THROUGH;
  idleTimer = 0;

  reads = 0;
  writes = 0;
  activates = 0;

  actWaits = 0;
  actWaitTime = 0;

  bankId = -1;

  refreshUsed = false;
  refreshRows = 1024;
  needsRefresh = false;

  psInterval = 0;

  nextCompletion = std::numeric_limits<ncycle_t>::max();
}



Bank::~Bank( )
{
  
}



void Bank::SetConfig( Config *c )
{
  conf = c;

  Params *params = new Params( );
  params->SetParams( c );
  SetParams( params );

  /*
   *  We need to create an endurance model on a bank-by-bank basis.
   */
  endrModel = EnduranceModelFactory::CreateEnduranceModel( p->EnduranceModel );
  if( endrModel )
    endrModel->SetConfig( conf );


  if( p->InitPD )
    state = BANK_PDPF;

  if( p->UseRefresh )
    {
      refreshUsed = true;
      refreshRows = p->RefreshRows;
      nextRefresh = GetEventQueue()->GetCurrentCycle() + (p->tRFI / (p->ROWS / refreshRows));
    }
}



void Bank::SetNextRefresh( ncycle_t nextREF )
{
  GetEventQueue()->InsertEvent( EventCycle, this, nextREF );
}



bool Bank::PowerDown( BankState pdState )
{
  bool returnValue = false;

  if( nextPowerDown <= GetEventQueue()->GetCurrentCycle() && ( state == BANK_OPEN || state == BANK_CLOSED ) )
    {
      /*
       *  The power down state (pdState) will be determined by the device class, which
       *  will check to see if all the banks are idle or not, and if fast exit is used.
       */
      state = pdState;

      nextPowerUp = MAX( nextPowerUp, GetEventQueue()->GetCurrentCycle() + p->tPD );
      nextActivate = MAX( nextActivate, GetEventQueue()->GetCurrentCycle() + p->tPD + p->tXP );
      if( pdState == BANK_PDPF || pdState == BANK_PDA )
        nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tPD + p->tXP );
      else
        nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tPD + p->tXPDLL );
      nextWrite = MAX( nextWrite, GetEventQueue()->GetCurrentCycle() + p->tPD + p->tXP );
      nextPrecharge = MAX( nextPrecharge, GetEventQueue()->GetCurrentCycle() + p->tPD + p->tXP );

      
      switch( nextCommand )
        {
        case CMD_PDPF:
          nextCommand = CMD_NOP;
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: PowerDown: Unknown bulk command: " << nextCommand << std::endl;
          nextCommand = CMD_NOP;
          break;
        }
     

      returnValue = true;
    }

  return returnValue;
}



bool Bank::PowerUp( NVMainRequest *request )
{
  bool returnValue = false;

  if( nextPowerUp <= GetEventQueue()->GetCurrentCycle() && ( state == BANK_PDPF || state == BANK_PDPS || state == BANK_PDA ) )
    {
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + p->tXP );
      nextActivate = MAX( nextActivate, GetEventQueue()->GetCurrentCycle() + p->tXP );
      if( state == BANK_PDPS )
        nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tXPDLL );
      else
        nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tXP );
      nextWrite = MAX( nextWrite, GetEventQueue()->GetCurrentCycle() + p->tXP );
      nextPrecharge = MAX( nextPrecharge, GetEventQueue()->GetCurrentCycle() + p->tXP );

      /*
       *  While technically the bank is being "powered up" we will just reset
       *  the previous state. For energy calculations, the bank is still considered
       *  to be consuming background power while powering up/down. Thus, we need
       *  a powerdown wait, but no power up wait.
       */
      if( state == BANK_PDA )
        state = BANK_OPEN;
      else
        state = BANK_CLOSED;


      /* Wakeup this class when the power up completes for implicit command issue. */
      if( nextCommand != CMD_NOP )
        GetEventQueue( )->InsertEvent( EventCycle, this, nextActivate );


      lastOperation = *request;

      switch( request->bulkCmd )
        {
        case CMD_PU_ACT_READ_PRE_PDPF:
          nextCommand = CMD_ACT_READ_PRE_PDPF;
          break;

        case CMD_PU_ACT_WRITE_PRE_PDPF:
          nextCommand = CMD_ACT_WRITE_PRE_PDPF;
          break;

        case CMD_PU_ACT_READ_PRE:
          nextCommand = CMD_ACTREADPRE;
          break;

        case CMD_PU_ACT_WRITE_PRE:
          nextCommand = CMD_ACTWRITEPRE;
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: PowerUp: Unknown bulk command: " << request->bulkCmd << std::endl;
          nextCommand = CMD_NOP;
          break;
        }


      returnValue = true;
    }

  return returnValue;
}


bool Bank::Activate( NVMainRequest *request )
{
  uint64_t activateRow;
  bool returnValue = false;

  request->address.GetTranslatedAddress( &activateRow, NULL, NULL, NULL, NULL );

  if( nextActivate <= GetEventQueue()->GetCurrentCycle() && state == BANK_CLOSED )
    {
      nextActivate = MAX( nextActivate, GetEventQueue()->GetCurrentCycle() + MAX( p->tRCD, p->tRAS ) 
                          + p->tRP );
      nextPrecharge = MAX( nextPrecharge, GetEventQueue()->GetCurrentCycle() + MAX( p->tRCD, p->tRAS ) );
      nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tRCD - p->tAL );
      nextWrite = MAX( nextWrite, GetEventQueue()->GetCurrentCycle() + p->tRCD - p->tAL );
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + p->tRCD + 1 );


      openRow = activateRow;
      state = BANK_OPEN;
      writeCycle = false;

      lastActivate = GetEventQueue()->GetCurrentCycle();

      /* Add to bank's total energy. */
      if( p->EnergyModel_set && p->EnergyModel == "current" )
        {
          /* DRAM Model */
          uint64_t tRC = p->tRAS + p->tRCD;

          bankEnergy += (float)((p->EIDD0 * (float)tRC) 
                      - ((p->EIDD3N * (float)p->tRAS)
                      +  (p->EIDD2N * (float)p->tRP)));

          activeEnergy +=  (float)((p->EIDD0 * (float)tRC) 
                        - ((p->EIDD3N * (float)p->tRAS)
                        +  (p->EIDD2N * (float)p->tRP) ));
        }
      else
        {
          /* Flat energy model. */
          bankEnergy += p->Erd;
        }


      lastOperation = *request;

      switch( request->bulkCmd )
        {
        case CMD_ACTREADPRE:
          nextCommand = CMD_READPRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_ACTREAD2PRE:
          nextCommand = CMD_READ2PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_ACTREAD3PRE:
          nextCommand = CMD_READ3PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_ACTREAD4PRE:
          nextCommand = CMD_READ4PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_ACTWRITEPRE:
          nextCommand = CMD_WRITEPRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_ACTWRITE2PRE:
          nextCommand = CMD_WRITE2PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_ACTWRITE3PRE:
          nextCommand = CMD_WRITE3PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_ACTWRITE4PRE:
          nextCommand = CMD_WRITE4PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_ACT_READ_PRE_PDPF:
          nextCommand = CMD_READ_PRE_PDPF;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;
          
        case CMD_ACT_WRITE_PRE_PDPF:
          nextCommand = CMD_WRITE_PRE_PDPF;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: Activate: Unknown bulk command: " << request->bulkCmd << std::endl;
          nextCommand = CMD_NOP;
          break;
        }


      activates++;

      returnValue = true;
    }
  else if( state == BANK_OPEN )
    {
      std::cout << "NVMain: Bank: Attempted to activate open row!" << std::endl;
    }

  return returnValue;
}



bool Bank::Read( NVMainRequest *request )
{
  uint64_t readRow, readCol;
  bool returnValue = false;

  request->address.GetTranslatedAddress( &readRow, &readCol, NULL, NULL, NULL );

  if( nextRead <= GetEventQueue()->GetCurrentCycle() && state == BANK_OPEN && readRow == openRow )
    {
      nextPrecharge = MAX( nextPrecharge, GetEventQueue()->GetCurrentCycle() + p->tAL + p->tBURST
               + p->tRTP - p->tCCD );
      nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + MAX( p->tBURST, p->tCCD ) );
      nextWrite = MAX( nextWrite, GetEventQueue()->GetCurrentCycle() + p->tCAS + p->tBURST 
               + 2 - p->tCWD );
      nextActivate = MAX( nextActivate, lastActivate + p->tRRDR );
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + p->tAL + p->tBURST
               + p->tCAS + 1 );
      

      dataCycles += p->tBURST;


      /*
       * Data is placed on the bus starting from tCAS and is complete after tBURST.
       * Wakeup owner at the end of this to notify that the whole request is complete.
       */
      if( bankId == 0 )
        GetEventQueue( )->InsertEvent( EventResponse, this, request, GetEventQueue()->GetCurrentCycle() + p->tCAS + MAX(p->tBURST, p->tCCD) );


      /* Calculate energy */
      if( p->EnergyModel_set && p->EnergyModel == "current" )
        {
          /* DRAM Model */
          bankEnergy += (float)((p->EIDD4R - p->EIDD3N) * (float)p->tBURST);

          burstEnergy += (float)((p->EIDD4R - p->EIDD3N) * (float)p->tBURST);
        }
      else
        {
          /* Flat Energy Model */
          bankEnergy += p->Eopenrd;

          burstEnergy += p->Eopenrd;
        }

      /*
       *  There's no reason to track data if endurance is not modeled.
       */
      if( conf->GetSimInterface( ) != NULL && endrModel != NULL )
        {
          NVMDataBlock dataBlock;

          /*
           *  In a trace-based simulation, or a live simulation where simulation is
           *  started in the middle of execution, some data being read maybe have never
           *  been written to memory. In this case, we will store the value, since the
           *  data is always correct from either the simulator or the value in the trace,
           *  which corresponds to the actual value that was read. 
           *
           *  Since the read data is already in the request, we don't actually need to
           *  copy it there.
           */
          if( !conf->GetSimInterface( )->GetDataAtAddress( request->address.GetPhysicalAddress( ), &dataBlock ) )
            {
              //std::cout << "Setting data block for 0x" << std::hex << request->address.GetPhysicalAddress( )
              //          << std::dec << " to " << request->data << std::endl;
              conf->GetSimInterface( )->SetDataAtAddress( request->address.GetPhysicalAddress( ), request->data );
            }
        }


      switch( request->bulkCmd )
        {
        case CMD_READPRE:
          nextCommand = CMD_PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_READ2PRE:
          nextCommand = CMD_READPRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_READ3PRE:
          nextCommand = CMD_READ2PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_READ4PRE:
          nextCommand = CMD_READ3PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_READ_PRE_PDPF:
          nextCommand = CMD_PRE_PDPF;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextRead );
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: Read: Unknown bulk command: " << request->bulkCmd << std::endl;
          nextCommand = CMD_NOP;
          break;
        }


      reads++;
      
      returnValue = true;
    }


  return returnValue;
}



bool Bank::Write( NVMainRequest *request )
{
  uint64_t writeRow, writeCol;
  bool returnValue = false;

  request->address.GetTranslatedAddress( &writeRow, &writeCol, NULL, NULL, NULL );

  if( nextWrite <= GetEventQueue()->GetCurrentCycle() && state == BANK_OPEN && writeRow == openRow )
    {
      nextPrecharge = MAX( nextPrecharge, GetEventQueue()->GetCurrentCycle() + p->tAL + p->tCWD
               + p->tBURST + p->tWR );
      nextRead = MAX( nextRead, GetEventQueue()->GetCurrentCycle() + p->tCWD 
              + p->tBURST + p->tWTR );
      nextWrite = MAX( nextWrite, GetEventQueue()->GetCurrentCycle() + MAX( p->tBURST, p->tCCD ) );
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + p->tAL + p->tBURST
               + p->tWR + p->tCWD + 1 );

      dataCycles += p->tBURST;

      
      /*
       *  Notify owner of write completion as well.
       */
      if( bankId == 0 )
        GetEventQueue( )->InsertEvent( EventResponse, this, request, GetEventQueue()->GetCurrentCycle() + p->tCWD + MAX(p->tBURST, p->tCCD) );


      /* Calculate energy. */
      if( p->EnergyModel_set && p->EnergyModel == "current" )
        {
          /* DRAM Model. */
          bankEnergy += (float)((p->EIDD4W - p->EIDD3N) * (float)p->tBURST);

          burstEnergy += (float)((p->EIDD4W - p->EIDD3N) * (float)p->tBURST);
        }
      else
        {
          /* Flat energy model. */
          bankEnergy += p->Ewr; // - p->Ewrpb * numUnchangedBits;

          burstEnergy += p->Ewr; // - p->Ewrpb * numUnchangedBits;
        }

      writeCycle = true;

      writes++;
      
      if( endrModel && bankId == 0 )
        {
          NVMDataBlock oldData;

          if( conf->GetSimInterface( ) != NULL )
            {
              /*
               *  If the old data is not there, we will assume the data is 0.
               */
              uint64_t wordSize;
              uint64_t blockMask;
              uint64_t blockAddr;
              bool hardError;

              wordSize = p->BusWidth;
              wordSize *= p->tBURST * p->RATE;
              wordSize /= 8;

              blockMask = ~(wordSize - 1);

              blockAddr = request->address.GetPhysicalAddress( ) & blockMask;
              //std::cout << "Address is 0x" << std::hex << request->address.GetPhysicalAddress() << " block mask is 0x" << blockMask << " block address is 0x" << blockAddr << std::dec << std::endl;

              if( !conf->GetSimInterface( )->GetDataAtAddress( request->address.GetPhysicalAddress( ), &oldData ) )
                {
                  // maybe increment a counter for this...
                  //std::cout << "Zeroing write to not-previously-read data 0x" << std::hex
                  //          << request->address.GetPhysicalAddress( ) << std::dec << std::endl;

                  for( int i = 0; i < (int)(p->BusWidth / 8); i++ )
                    oldData.SetByte( i, 0 );
                }
          
              /*
               *  Write the new data...
               */
              conf->GetSimInterface( )->SetDataAtAddress( request->address.GetPhysicalAddress( ), request->data );
     
              //std::cout << "Changing data at address 0x" << std::hex << request->address.GetPhysicalAddress( )
              //          << std::dec << " from " << oldData << " to " << request->data << std::endl;

              /*
               *  Model the endurance 
               */
              hardError = !endrModel->Write( request->address, oldData, request->data );

              if( hardError )
                {
                  std::cout << "WARNING: Write to 0x" << std::hex << request->address.GetPhysicalAddress( )
                            << std::dec << " resulted in a hard error! " << std::endl;
                }
            }
          else
            {
              std::cerr << "Warning: Endurance modeled without simulator interface for data tracking!" << std::endl;
            }
        }

      /* Determine next implicit command. */
      switch( request->bulkCmd )
        {
        case CMD_WRITEPRE:
          nextCommand = CMD_PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_WRITE2PRE:
          nextCommand = CMD_WRITEPRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_WRITE3PRE:
          nextCommand = CMD_WRITE2PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_WRITE4PRE:
          nextCommand = CMD_WRITE3PRE;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_WRITE_PRE_PDPF:
          nextCommand = CMD_PRE_PDPF;
          GetEventQueue( )->InsertEvent( EventCycle, this, nextWrite );
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: Write: Unknown bulk command: " << request->bulkCmd << std::endl;
          nextCommand = CMD_NOP;
          break;
        }



      returnValue = true;
    }



  return returnValue;
}



bool Bank::Precharge( NVMainRequest *request )
{
  bool returnValue = false;


  if( nextPrecharge <= GetEventQueue()->GetCurrentCycle() && state == BANK_OPEN  )
    {
      nextActivate = MAX( nextActivate, GetEventQueue()->GetCurrentCycle() + p->tRP );
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + p->tRP );


      if( bankId == 0 )
      {
        notifyComplete.insert(std::pair<NVMainRequest *, ncycle_t>( request, GetEventQueue()->GetCurrentCycle() + p->tRP ) );
        nextCompletion = std::min( nextCompletion, GetEventQueue()->GetCurrentCycle() + p->tRP );
      }



      switch( nextCommand )
        {
        case CMD_PRE:
          nextCommand = CMD_NOP;
          break;

        case CMD_PRE_PDPF:
          nextCommand = CMD_PDPF;
          GetEventQueue( )->InsertEvent( EventCycle, this, GetEventQueue()->GetCurrentCycle() + p->tRP );
          break;

        case CMD_NOP:
          nextCommand = CMD_NOP;
          break;

        default:
          std::cout << "Warning: Prechange: Unknown bulk command: " << nextCommand << std::endl;
          nextCommand = CMD_NOP;
          break;
        }

      state = BANK_CLOSED;

      returnValue = true;
    }


  return returnValue;
}


bool Bank::Refresh( )
{
  bool returnValue = false;

  if( nextRefresh <= GetEventQueue()->GetCurrentCycle() && state == BANK_CLOSED )
    {
      nextActivate = MAX( nextActivate, GetEventQueue()->GetCurrentCycle() + refreshRows * p->tRFC );
      nextPowerDown = MAX( nextPowerDown, GetEventQueue()->GetCurrentCycle() + refreshRows * p->tRFC );

      refreshRowIndex = (refreshRowIndex + refreshRows) % p->ROWS;

      /*
       *  tRFI is the minimum refresh time for all rows. If we don't refresh the entire
       *  bank we need to calculate when the next refresh will occur.. 
       */
      nextRefresh = GetEventQueue()->GetCurrentCycle() + ((p->tRFI) / (p->ROWS / refreshRows));
      needsRefresh = false;

      GetEventQueue()->InsertEvent( EventCycle, this, nextRefresh );
      
      if( p->EnergyModel_set && p->EnergyModel == "current" )
        {
          bankEnergy += (float)((p->EIDD5B - p->EIDD3N) * (float)p->tRFC * (float)refreshRows); 

          refreshEnergy += (float)((p->EIDD5B - p->EIDD3N) * (float)p->tRFC * (float)refreshRows); 
        }
      else
        {
          bankEnergy += p->Eref;

          refreshEnergy += p->Eref;
        }

      returnValue = true;
    }

  return returnValue;
}


bool Bank::IsIssuable( NVMainRequest *req, ncycle_t delay )
{
  uint64_t opRank;
  uint64_t opBank;
  uint64_t opRow;
  uint64_t opCol;
  bool rv = true;

  req->address.GetTranslatedAddress( &opRow, &opCol, &opBank, &opRank, NULL );

  if( nextCommand != CMD_NOP )
    return false;
    

  if( req->type == ACTIVATE )
    {
      if( nextActivate > (GetEventQueue()->GetCurrentCycle()+delay) || state != BANK_CLOSED )
        rv = false;

      if( refreshUsed && ((GetEventQueue()->GetCurrentCycle()+delay) >= nextRefresh || needsRefresh) )
        rv = false;

      if( rv == false )
        {
          if( nextActivate > (GetEventQueue()->GetCurrentCycle()+delay) )
            {
              //std::cout << "Bank: Can't activate for " << nextActivate - GetEventQueue()->GetCurrentCycle() << " cycles." << std::endl;
              actWaits++;
              actWaitTime += nextActivate - (GetEventQueue()->GetCurrentCycle()+delay);
            }
        }
    }
  else if( req->type == READ )
    {
      if( nextRead > (GetEventQueue()->GetCurrentCycle()+delay) || state != BANK_OPEN || opRow != openRow || ( refreshUsed && needsRefresh ) )
        rv = false;
    }
  else if( req->type == WRITE )
    {
      if( nextWrite > (GetEventQueue()->GetCurrentCycle()+delay) || state != BANK_OPEN || opRow != openRow || ( refreshUsed && needsRefresh ) )
        rv = false;
    }
  else if( req->type == PRECHARGE )
    {
      if( nextPrecharge > (GetEventQueue()->GetCurrentCycle()+delay) || state != BANK_OPEN )
        rv = false;
    }
  else if( req->type == POWERDOWN_PDA || req->type == POWERDOWN_PDPF || req->type == POWERDOWN_PDPS )
    {
      if( nextPowerDown > (GetEventQueue()->GetCurrentCycle()+delay) || ( state != BANK_OPEN && state != BANK_CLOSED ) || ( refreshUsed && needsRefresh ) )
        rv = false;
    }
  else if( req->type == POWERUP )
    {
      if( nextPowerUp > (GetEventQueue()->GetCurrentCycle()+delay) || ( state != BANK_PDPF && state != BANK_PDPS && state != BANK_PDA ) || ( refreshUsed && needsRefresh ) )
        rv = false;
    }
  else if( req->type == REFRESH )
    {
      if( nextRefresh > (GetEventQueue()->GetCurrentCycle()+delay) || state != BANK_CLOSED || needsRefresh )
        rv = false;
    }
  else
    {
      std::cout << "Bank: IsIssuable: Unknown operation: " << req->type << std::endl;
      rv = false;
    }

  return rv;
}


bool Bank::WouldConflict( uint64_t checkRow )
{
  bool returnValue = true;

  if ( state == BANK_OPEN && checkRow == openRow )
    returnValue = false;

  return returnValue;
}



BankState Bank::GetState( ) 
{
  return state;
}


float Bank::GetPower( )
{
  /*
   *
   */
  float simulationTime = (float)((float)GetEventQueue()->GetCurrentCycle() / ((float)p->CLK * 1000000.0f));
  float power = 0.0f;

  if( simulationTime == 0.0f )
    return 0.0f;

  if( p->EnergyModel_set && p->EnergyModel == "current" )
    {
      power = ((backgroundEnergy / (float)GetEventQueue()->GetCurrentCycle()) * p->Voltage) / 1000.0f;
      power = ((activeEnergy / (float)GetEventQueue()->GetCurrentCycle()) * p->Voltage) / 1000.0f;
      power = ((refreshEnergy / (float)GetEventQueue()->GetCurrentCycle()) * p->Voltage) / 1000.0f;
      power = ((bankEnergy / (float)GetEventQueue()->GetCurrentCycle()) * p->Voltage) / 1000.0f;
    }
  else
    power = ((bankEnergy / 1000000.0f) / simulationTime);

  return power;
}



void Bank::SetName( std::string )
{
}


// Corresponds to physical bank id e.g., if this bank logically spans multiple devices, the id corresponds to the device, NOT the logical bank id within a single device.
void Bank::SetId( int id )
{
  bankId = id;
}



void Bank::PrintStats( )
{
  float idealBandwidth;


  idealBandwidth = (float)(p->CLK * p->MULT * 
                   p->RATE * p->BPC);

  if( activeCycles != 0 )
    utilization = (float)((float)dataCycles / (float)activeCycles);
  else
    utilization = 0.0f;


  if( p->EnergyModel_set && p->EnergyModel == "current" )
    {
      std::cout << "i" << psInterval << "." << statName << ".current " << bankEnergy << "\t; mA" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".current.background " << backgroundEnergy << "\t; mA" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".current.active " << activeEnergy << "\t; mA" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".current.burst " << burstEnergy << "\t; mA" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".current.refresh " << refreshEnergy << "\t; mA" << std::endl;
    }
  else
    {
      std::cout << "i" << psInterval << "." << statName << ".energy " << bankEnergy << "\t; nJ" << std::endl; 
      std::cout << "i" << psInterval << "." << statName << ".energy.background " << backgroundEnergy << "\t; nJ" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".energy.active " << activeEnergy << "\t; nJ" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".energy.burst " << burstEnergy << "\t; nJ" << std::endl;
      std::cout << "i" << psInterval << "." << statName << ".energy.refresh " << refreshEnergy << "\t; nJ" << std::endl;
    }
  
  std::cout << "i" << psInterval << "." << statName << ".power " << GetPower( ) << "\t; W per bank per device" << std::endl
            << "i" << psInterval << "." << statName << ".bandwidth " << (utilization * idealBandwidth) << "\t; MB/s "
            << "i" << psInterval << "." << statName << "(" << dataCycles << " data cycles in " << activeCycles << " cycles)" << std::endl
            << "i" << psInterval << "." << statName << ".utilization " << utilization << std::endl;
  std::cout << "i" << psInterval << "." << statName << ".reads " << reads << std::endl
            << "i" << psInterval << "." << statName << ".writes " << writes << std::endl
            << "i" << psInterval << "." << statName << ".activates " << activates << std::endl;
  std::cout << "i" << psInterval << "." << statName << ".activeCycles " << powerCycles << std::endl
            << "i" << psInterval << "." << statName << ".fastExitCycles " << feCycles << std::endl
            << "i" << psInterval << "." << statName << ".slowExitCycles " << seCycles << std::endl;

  if( endrModel )
    {
      if( endrModel->GetWorstLife( ) == std::numeric_limits< uint64_t >::max( ) )
        std::cout << "i" << psInterval << "." << statName << ".worstCaseEndurance N/A" << std::endl
                  << "i" << psInterval << "." << statName << ".averageEndurance N/A" << std::endl;
      else
        std::cout << "i" << psInterval << "." << statName << ".worstCaseEndurance " << (endrModel->GetWorstLife( )) << std::endl
                  << "i" << psInterval << "." << statName << ".averageEndurance " << endrModel->GetAverageLife( ) << std::endl;

      endrModel->PrintStats( );
    }

  std::cout << "i" << psInterval << "." << statName << ".actWaits " << actWaits << std::endl
            << "i" << psInterval << "." << statName << ".actWaits.totalTime " << actWaitTime << std::endl
            << "i" << psInterval << "." << statName << ".actWaits.averageTime " << (double)((double)actWaitTime / (double)actWaits) << std::endl;

  psInterval++;
}


bool Bank::Idle( )
{
  if( nextPrecharge <= GetEventQueue()->GetCurrentCycle() 
      && nextActivate <= GetEventQueue()->GetCurrentCycle()
      && nextRead <= GetEventQueue()->GetCurrentCycle() 
      && nextWrite <= GetEventQueue()->GetCurrentCycle()
      && ( state == BANK_CLOSED || state == BANK_OPEN ) )
    {
      return true;
    }

  return false;
}



void Bank::IssueImplicit( )
{
  if( nextCommand != CMD_NOP )
    {
      NVMainRequest req;
      BulkCommand bulkCmd;
      ncycle_t issuableCycle = 0;
      bool foundCycle = false;

      req.address = lastOperation.address;
      
      switch( nextCommand )
        {
        case CMD_PDPF:
          req.type = POWERDOWN_PDPF;
          lastOperation.type = POWERDOWN_PDPF;
          break;

        case CMD_ACT_READ_PRE_PDPF:
        case CMD_ACT_WRITE_PRE_PDPF:
        case CMD_ACTREADPRE:
        case CMD_ACTWRITEPRE:
          req.type = ACTIVATE;
          lastOperation.type = ACTIVATE;
          break;

        case CMD_PRE:
        case CMD_PRE_PDPF:
          req.type = PRECHARGE;
          lastOperation.type = PRECHARGE;
          break;

        case CMD_READPRE:
        case CMD_READ2PRE:
        case CMD_READ3PRE:
        case CMD_READ4PRE:
        case CMD_READ_PRE_PDPF:
          req.type = READ;
          lastOperation.type = READ;
          break;

        case CMD_WRITEPRE:
        case CMD_WRITE2PRE:
        case CMD_WRITE3PRE:
        case CMD_WRITE4PRE:
        case CMD_WRITE_PRE_PDPF:
          req.type = WRITE;
          lastOperation.type = WRITE;
          break;

        default:
          std::cout << "Warning: Invalid nextCommand: " << nextCommand << std::endl;
          break;
        }
      

      /*
       *  IsIssuable returns false if nextCommand != CMD_NOP. This is meant for other
       *  bulk commands being issued from the interconnect. Here it is temporarily
       *  set to CMD_NOP before calling IsIssuable.
       */
      bulkCmd = nextCommand;
      nextCommand = CMD_NOP;
      if( IsIssuable( &req ) )
        {
          nextCommand = bulkCmd;
          lastOperation.bulkCmd = bulkCmd;

          switch( req.type )
            {
            case PRECHARGE:
              Precharge( &lastOperation );
              break;

            case READ:
              Read( &lastOperation );
              break;

            case WRITE:
              Write( &lastOperation );
              break;

            case ACTIVATE:
              Activate( &lastOperation );
              break;

            case POWERDOWN_PDA:
              PowerDown( BANK_PDA );
              break;

            case POWERDOWN_PDPF:
              PowerDown( BANK_PDPF );
              break;
              
            case POWERDOWN_PDPS:
              PowerDown( BANK_PDPS );
              break;
          
            case REFRESH:
              Refresh( );
              break;

            default:
              break;
            }
        }
      /*
       *  Implicit command couldn't be issued. Retry when the bank reports the
       *  request is issuable.
       */
      else
        {
          nextCommand = bulkCmd;

          switch( req.type )
            {
            case PRECHARGE:
              issuableCycle = GetNextPrecharge( );
              foundCycle = true;
              break;

            case READ:
              issuableCycle = GetNextRead( );
              foundCycle = true;
              break;

            case WRITE:
              issuableCycle = GetNextWrite( );
              foundCycle = true;
              break;

            case ACTIVATE:
              issuableCycle = GetNextActivate( );
              foundCycle = true;
              break;

            case POWERDOWN_PDA:
              issuableCycle = GetNextPowerDown( );
              foundCycle = true;
              break;

            case POWERDOWN_PDPF:
              issuableCycle = GetNextPowerDown( );
              foundCycle = true;
              break;
              
            case POWERDOWN_PDPS:
              issuableCycle = GetNextPowerDown( );
              foundCycle = true;
              break;
          
            case REFRESH:
              issuableCycle = GetNextRefresh( );
              foundCycle = true;
              break;

            default:
              break;
            }


          /* Couldn't issue. Retry on the next issuable cycle. */
          if( foundCycle )
            GetEventQueue( )->InsertEvent( EventCycle, this, issuableCycle );
        }
    }
}


void Bank::Cycle( ncycle_t steps )
{
  /* Check for implicit commands to issue. */
  IssueImplicit( );

  
  /* Check for state updates. */


  /*
   *  Automatically refresh if we need to.
   */
  if( refreshUsed && nextRefresh <= GetEventQueue()->GetCurrentCycle() )
    {
      if( state == BANK_CLOSED )
        {
          Refresh( );
        }
      else
        {
          needsRefresh = true;
        }
    }



  /*
   *  Count non-idle cycles for utilization calculations
   */
  if( !Idle( ) )
    {
      activeCycles += steps;
      
      /*
       *  If we are in a powered-down state, count the number of cycles spent in each state.
       */
      if( state == BANK_PDPF || state == BANK_PDA )
        feCycles += steps;
      else if( state == BANK_PDPS )
        seCycles += steps;
      else
        powerCycles += steps;
    }
}


