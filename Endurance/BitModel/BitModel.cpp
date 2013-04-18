/*******************************************************************************
* Copyright (c) 2012-2013, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
* 
* This source code is part of NVMain - A cycle accurate timing, bit accurate
* energy simulator for both volatile (e.g., DRAM) and nono-volatile memory
* (e.g., PCRAM). The source code is free and you can redistribute and/or
* modify it by providing that the following conditions are met:
* 
*  1) Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
* 
*  2) Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the documentation
*     and/or other materials provided with the distribution.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* Author list: 
*   Matt Poremba    ( Email: mrp5060 at psu dot edu 
*                     Website: http://www.cse.psu.edu/~poremba/ )
*******************************************************************************/

#include "Endurance/BitModel/BitModel.h"
#include <iostream>

using namespace NVM;

BitModel::BitModel( )
{
    /*
     *  Clear the life map, which will hold all of the endurance
     *  values for each of our rows. Do this to ensure it didn't
     *  happen to be allocated somewhere that thinks it contains 
     *  values.
     */
    life.clear( );

    SetGranularity( 1 );
}

BitModel::~BitModel( )
{
    /*
     *  Nothing to do here. We do not own the *config pointer, so
     *  don't delete that.
     */
}

bool BitModel::Write( NVMAddress address, NVMDataBlock oldData, 
                      NVMDataBlock newData )
{
    /*
     *  The default life map is an stl map< uint64_t, uint64_t >. 
     *  You may map row and col to this map_key however you want.
     *  It is up to you to ensure there are no collisions here.
     */
    uint64_t row, subarray, MATHeight;
    uint64_t col;
    bool rv = true;
    NVMAddress faultAddr;

    /* For our simple row model, we just set the key equal to the row */
    address.GetTranslatedAddress( &row, &col, NULL, NULL, NULL, &subarray );
    faultAddr = address;
    
    /*
     *  If using the default life map, we can call the DecrementLife
     *  function which will check if the map_key already exists. If so,
     *  the life value is decremented (write count incremented). Otherwise 
     *  the map_key is inserted with a write count of 1.
     */
    uint64_t wordkey;
    uint64_t rowSize;
    uint64_t wordSize;
    uint64_t partitionCount;

    MATHeight = GetConfig( )->GetValue( "MATHeight" );

    rowSize = GetConfig( )->GetValue( "COLS" );
    
    wordSize = GetConfig( )->GetValue( "BusWidth" );
    wordSize *= GetConfig( )->GetValue( "tBURST" ) 
                    * GetConfig( )->GetValue( "RATE" );
    wordSize /= 8;

    /* Check each byte to see if it was modified */
    for( uint64_t i = 0; i < wordSize; ++i )
    {
        /*
         *  If no bytes have changed we can just continue. Yes, I know this
         *  will check the byte 8 times, but i'd rather not change the iter.
         */
        uint8_t oldByte, newByte;

        oldByte = oldData.GetByte( i );
        newByte = newData.GetByte( i );

        if( oldByte == newByte ) 
            continue;

        /*
         *  If the bytes are different, then at least one bit has changed.
         *  check each bit individually.
         */
        for( int j = 0; j < 8; j++ )
        {
            uint8_t oldBit, newBit;

            oldBit = ( oldByte >> j ) & 0x1;
            newBit = ( newByte >> j ) & 0x1;

            if( oldBit == newBit )
                continue;

            std::cout << "Bit " << j << " changed in byte " << i << std::endl;

            /*
             *  Think of each row being partitioned into 1-bit divisions. 
             *  Each row has rowSize * 8 paritions. For the key we will use:
             *
             *  row * number of partitions + partition in this row
             */
            partitionCount = rowSize * 8;
            
            wordkey = ( row + MATHeight * subarray ) * partitionCount 
                             + (col * wordSize * 8) + i * 8 + j;

            std::cout << "Key is " << wordkey << std::endl;

            faultAddr.SetBitAddress( static_cast<uint8_t>(j) );
            faultAddr.SetPhysicalAddress( address.GetPhysicalAddress( ) + i );
            if( !DecrementLife( wordkey, faultAddr ) )
                rv = false;
        }
    }

    return rv;
}
