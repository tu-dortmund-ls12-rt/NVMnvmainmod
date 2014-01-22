/*******************************************************************************
* Copyright (c) 2012-2013, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
* 
* This source code is part of NVMain - A cycle accurate timing, bit accurate
* energy simulator for both volatile (e.g., DRAM) and non-volatile memory
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


#include "src/Stats.h"
#include "include/NVMTypes.h"


using namespace NVM;


Stats::Stats( )
{

}

Stats::~Stats( )
{
    std::vector<StatBase *>::iterator it;

    for( it = statList.begin(); it != statList.end(); it++ )
    {
        /* Free reset value memory. */
        int *rval = static_cast<int *>((*it)->GetResetValue( ));
        delete rval; 
        delete (*it);
    }
}

void Stats::addStat( void *stat, void *resetValue, std::string statType, size_t typeSize, std::string name, std::string units )
{
    StatBase *sb = new StatBase( );

    sb->SetName( name );
    sb->SetValue( stat );
    sb->SetResetValue( resetValue );
    sb->SetStatType( statType, typeSize );
    sb->SetUnits( units );

    statList.push_back( sb );
}

void Stats::removeStat( void *stat )
{
    std::vector<StatBase *>::iterator it;

    for( it = statList.begin(); it != statList.end(); it++ )
    {
        if( (*it)->GetValue( ) == stat )
        {
            /* Free reset value memory. */
            int *rval = static_cast<int *>((*it)->GetResetValue( ));
            delete rval; 

            statList.erase( it );
            break;
        }
    }
}

void Stats::PrintAll( std::ostream& stream )
{
    std::vector<StatBase *>::iterator it;

    for( it = statList.begin(); it != statList.end(); it++ )
    {
        (*it)->Print( stream );
    }
}

void Stats::ResetAll( )
{
    std::vector<StatBase *>::iterator it;

    for( it = statList.begin(); it != statList.end(); it++ )
    {
        (*it)->Reset( );
    }
}


void StatBase::Reset( )
{
    std::memcpy( value, resetValue, typeSize );
}

void StatBase::Print( std::ostream& stream )
{
    stream << "i" << psInterval << "." << name << " ";

    if( statType == typeid(int).name() ) stream << *(static_cast<int *>(value));
    else if( statType == typeid(float).name() ) stream << *(static_cast<float *>(value));
    else if( statType == typeid(double).name() ) stream << *(static_cast<double *>(value));
    else if( statType == typeid(ncounter_t).name() ) stream << *(static_cast<ncounter_t *>(value));
    else if( statType == typeid(ncounters_t).name() ) stream << *(static_cast<ncounters_t *>(value));
    else if( statType == typeid(ncycle_t).name() ) stream << *(static_cast<ncycle_t *>(value));
    else if( statType == typeid(ncycles_t).name() ) stream << *(static_cast<ncycles_t *>(value));
    else if( statType == typeid(std::string).name() ) stream << *(static_cast<std::string *>(value));
    else stream << "?????";

    stream << units << std::endl;

    psInterval++;
}

