/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "interference-helper.h"
#include "wifi-phy.h"
#include "error-rate-model.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm>

NS_LOG_COMPONENT_DEFINE ("InterferenceHelper");

namespace ns3 {

class AntennaListenerInterferenceHelper : public ns3::WifiAntennaListener
{
public:
  /**
   * Create a AntennaListener for the given InterferenceHelper.
   *
   * \param state
   */
  AntennaListenerInterferenceHelper (ns3::InterferenceHelper *interference)
    : m_interference (interference)
  {
  }
  virtual ~AntennaListenerInterferenceHelper ()
  {
  }
  virtual void NotifyChangeAntennaMode (int mode)
  {
    m_interference->NotifyChangeAntennaModeNow (mode);
  }
private:
  ns3::InterferenceHelper *m_interference;  //!< InterferenceHelper to forward events to
};

/****************************************************************
 *       Phy event class
 ****************************************************************/

InterferenceHelper::Event::Event (uint32_t size, WifiMode payloadMode,
                                  enum WifiPreamble preamble,
                                  Time duration, double rxPower[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES],
                                  WifiTxVector txVector)
  : m_size (size),
    m_payloadMode (payloadMode),
    m_preamble (preamble),
    m_startTime (Simulator::Now ()),
    m_endTime (m_startTime + duration),
    m_txVector (txVector)
{
  for(int i = 0; i < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; i++){
    m_rxPowerW [i] =  rxPower [i];
  }
}
InterferenceHelper::Event::~Event ()
{
}

Time
InterferenceHelper::Event::GetDuration (void) const
{
  return m_endTime - m_startTime;
}
Time
InterferenceHelper::Event::GetStartTime (void) const
{
  return m_startTime;
}
Time
InterferenceHelper::Event::GetEndTime (void) const
{
  return m_endTime;
}
double
InterferenceHelper::Event::GetRxPowerW (int mode) const
{
  return m_rxPowerW [mode];
}
double*
InterferenceHelper::Event::GetAllRxPowerW (void)
{
  return m_rxPowerW;
}
uint32_t
InterferenceHelper::Event::GetSize (void) const
{
  return m_size;
}
WifiMode
InterferenceHelper::Event::GetPayloadMode (void) const
{
  return m_payloadMode;
}
enum WifiPreamble
InterferenceHelper::Event::GetPreambleType (void) const
{
  return m_preamble;
}

WifiTxVector
InterferenceHelper::Event::GetTxVector (void) const
{
  return m_txVector;
}


/****************************************************************
 *       Class which records SNIR change events for a
 *       short period of time.
 ****************************************************************/

InterferenceHelper::NiChange::NiChange (Time time, double* delta)
  : m_time (time)
{
  for(int i = 0; i < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; i++){
    m_delta [i] = delta [i];
    NS_LOG_DEBUG ("ddd" << m_delta [i]);
  }
}
Time
InterferenceHelper::NiChange::GetTime (void) const
{
  return m_time;
}
double
InterferenceHelper::NiChange::GetDelta (int mode) const
{
  return m_delta [mode];
}
double*
InterferenceHelper::NiChange::GetDelta ()
{
  return m_delta;
}
bool
InterferenceHelper::NiChange::operator < (const InterferenceHelper::NiChange& o) const
{
  return (m_time < o.m_time);
}

/****************************************************************
 *       The actual InterferenceHelper
 ****************************************************************/

InterferenceHelper::InterferenceHelper ()
  : m_errorRateModel (0),
    m_firstPower (0.0),
    m_rxing (false),
    m_antennaMode (0),
    m_antennaListener (0)
{
}
InterferenceHelper::~InterferenceHelper ()
{
  EraseEvents ();
  m_errorRateModel = 0;
}

Ptr<InterferenceHelper::Event>
InterferenceHelper::Add (uint32_t size, WifiMode payloadMode,
                         enum WifiPreamble preamble,
                         Time duration, double rxPowerW[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES],
                         WifiTxVector txVector)
{
  Ptr<InterferenceHelper::Event> event;

  event = Create<InterferenceHelper::Event> (size,
                                             payloadMode,
                                             preamble,
                                             duration,
                                             rxPowerW,
                                             txVector);
  AppendEvent (event);
  return event;
}


void
InterferenceHelper::SetNoiseFigure (double value)
{
  m_noiseFigure = value;
}

double
InterferenceHelper::GetNoiseFigure (void) const
{
  return m_noiseFigure;
}

void
InterferenceHelper::SetErrorRateModel (Ptr<ErrorRateModel> rate)
{
  m_errorRateModel = rate;
}

Ptr<ErrorRateModel>
InterferenceHelper::GetErrorRateModel (void) const
{
  return m_errorRateModel;
}

Time
InterferenceHelper::GetEnergyDuration (double energyW)
{
  Time now = Simulator::Now ();
  double noiseInterferenceW = 0.0;
  Time end = now;
  noiseInterferenceW = m_firstPower;
  for (NiChanges::const_iterator i = m_niChanges.begin (); i != m_niChanges.end (); i++)
    {
      noiseInterferenceW += i->GetDelta (m_antennaMode);
      end = i->GetTime ();
      if (end < now)
        {
          continue;
        }
      if (noiseInterferenceW < energyW)
        {
          break;
        }
    }
  return end > now ? end - now : MicroSeconds (0);
}

void
InterferenceHelper::AppendEvent (Ptr<InterferenceHelper::Event> event)
{
  Time now = Simulator::Now ();
  /*
  if (!m_rxing)
    {
      NiChanges::iterator nowIterator = GetPosition (now);
      for (NiChanges::iterator i = m_niChanges.begin (); i != nowIterator; i++)
        {
          m_firstPower += i->GetDelta (m_antennaMode);
        }
      m_niChanges.erase (m_niChanges.begin (), nowIterator);
      m_niChanges.insert (m_niChanges.begin (), NiChange (event->GetStartTime (), event->GetAllRxPowerW ()));
    }
  else
    {
      AddNiChangeEvent (NiChange (event->GetStartTime (), event->GetAllRxPowerW ()));
    }
  */
  AddNiChangeEvent (NiChange (event->GetStartTime (), event->GetAllRxPowerW ()));
  double rxPowerW[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
  for(int i = 0; i < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; i++){
    rxPowerW[i] = -event->GetAllRxPowerW ()[i];
  }
  AddNiChangeEvent (NiChange (event->GetEndTime (), rxPowerW));

}


double
InterferenceHelper::CalculateSnr (double signal, double noiseInterference, WifiMode mode) const
{
  // thermal noise at 290K in J/s = W
  static const double BOLTZMANN = 1.3803e-23;
  // Nt is the power of thermal noise in W
  double Nt = BOLTZMANN * 290.0 * mode.GetBandwidth ();
  // receiver noise Floor (W) which accounts for thermal noise and non-idealities of the receiver
  double noiseFloor = m_noiseFigure * Nt;
  double noise = noiseFloor + noiseInterference;
  double snr = signal / noise;
  NS_LOG_DEBUG("signal " << signal << " noise" << noise);
  return snr;
}

double
InterferenceHelper::CalculateNoiseInterferenceW (Ptr<InterferenceHelper::Event> event, NiChanges *ni) const
{
  double noiseInterference = m_firstPower;
  NS_ASSERT (m_rxing);
  for (NiChanges::const_iterator i = m_niChanges.begin () + 1; i != m_niChanges.end (); i++)
    {
      if ((event->GetEndTime () == i->GetTime ()) && event->GetRxPowerW (m_antennaMode) == -i->GetDelta (m_antennaMode))
        {
          break;
        }
      ni->push_back (*i);
    }
  double noise[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
  double zero [WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
  for(int i = 0; i <WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; i++){
    noise[i] = noiseInterference;
    zero[i] = 0;
  }
  ni->insert (ni->begin (), NiChange (event->GetStartTime (), noise));
  ni->push_back (NiChange (event->GetEndTime (), zero));
  return noiseInterference;
}

double
InterferenceHelper::CalculateChunkSuccessRate (double snir, Time duration, WifiMode mode) const
{
  if (duration == NanoSeconds (0))
    {
      return 1.0;
    }
  uint32_t rate = mode.GetPhyRate ();
  uint64_t nbits = (uint64_t)(rate * duration.GetSeconds ());
  double csr = m_errorRateModel->GetChunkSuccessRate (mode, snir, (uint32_t)nbits);
  return csr;
}

double
InterferenceHelper::CalculatePer (Ptr<const InterferenceHelper::Event> event, NiChanges *ni) const
{
  double psr = 1.0; /* Packet Success Rate */
  NiChanges::iterator j = ni->begin ();
  Time previous = (*j).GetTime ();
  WifiMode payloadMode = event->GetPayloadMode ();
  WifiPreamble preamble = event->GetPreambleType ();
 WifiMode MfHeaderMode ;
 if (preamble==WIFI_PREAMBLE_HT_MF)
   {
    MfHeaderMode = WifiPhy::GetMFPlcpHeaderMode (payloadMode, preamble); //return L-SIG mode

   }
  WifiMode headerMode = WifiPhy::GetPlcpHeaderMode (payloadMode, preamble);
  Time plcpHeaderStart = (*j).GetTime () + MicroSeconds (WifiPhy::GetPlcpPreambleDurationMicroSeconds (payloadMode, preamble)); //packet start time+ preamble
  Time plcpHsigHeaderStart=plcpHeaderStart+ MicroSeconds (WifiPhy::GetPlcpHeaderDurationMicroSeconds (payloadMode, preamble));//packet start time+ preamble+L SIG
  Time plcpHtTrainingSymbolsStart = plcpHsigHeaderStart + MicroSeconds (WifiPhy::GetPlcpHtSigHeaderDurationMicroSeconds (payloadMode, preamble));//packet start time+ preamble+L SIG+HT SIG
  Time plcpPayloadStart =plcpHtTrainingSymbolsStart + MicroSeconds (WifiPhy::GetPlcpHtTrainingSymbolDurationMicroSeconds (payloadMode, preamble,event->GetTxVector())); //packet start time+ preamble+L SIG+HT SIG+Training
  double noiseInterferenceW = (*j).GetDelta (m_antennaMode);
  double powerW = event->GetRxPowerW (m_antennaMode);
    j++;
  while (ni->end () != j)
    {
      Time current = (*j).GetTime ();
      NS_ASSERT (current >= previous);
      //Case 1: Both prev and curr point to the payload
      if (previous >= plcpPayloadStart)
        {
          psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                          noiseInterferenceW,
                                                          payloadMode),
                                            current - previous,
                                            payloadMode);
        }
      //Case 2: previous is before payload
      else if (previous >= plcpHtTrainingSymbolsStart)
        {
          //Case 2a: current is after payload
          if (current >= plcpPayloadStart)
            { 
               //Case 2ai and 2aii: All formats
               psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              payloadMode),
                                                current - plcpPayloadStart,
                                                payloadMode);
                
              }
        }
      //Case 3: previous is in HT-SIG: Non HT will not enter here since it didn't enter in the last two and they are all the same for non HT
      else if (previous >=plcpHsigHeaderStart)
        {
          //Case 3a: cuurent after payload start
          if (current >=plcpPayloadStart)
             {
                   psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              payloadMode),
                                                current - plcpPayloadStart,
                                                payloadMode);
                 
                    psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              headerMode),
                                               plcpHtTrainingSymbolsStart - previous,
                                                headerMode);
              }
          //case 3b: current after HT training symbols start
          else if (current >=plcpHtTrainingSymbolsStart)
             {
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                   plcpHtTrainingSymbolsStart - previous,
                                                   headerMode);  
                   
             }
         //Case 3c: current is with previous in HT sig
         else
            {
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                   current- previous,
                                                   headerMode);  
                   
            }
      }
      //Case 4: previous in L-SIG: GF will not reach here because it will execute the previous if and exit
      else if (previous >= plcpHeaderStart)
        {
          //Case 4a: current after payload start  
          if (current >=plcpPayloadStart)
             {
                   psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              payloadMode),
                                                      current - plcpPayloadStart,
                                                      payloadMode);
                    //Case 4ai: Non HT format (No HT-SIG or Training Symbols)
              if (preamble == WIFI_PREAMBLE_LONG || preamble == WIFI_PREAMBLE_SHORT) //plcpHtTrainingSymbolsStart==plcpHeaderStart)
                {
                    psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              headerMode),
                                                plcpPayloadStart - previous,
                                                headerMode);
                }

               else{
                    psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              headerMode),
                                                      plcpHtTrainingSymbolsStart - plcpHsigHeaderStart,
                                                      headerMode);
                    psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                    noiseInterferenceW,
                                                                    MfHeaderMode),
                                                      plcpHsigHeaderStart - previous,
                                                      MfHeaderMode);
                 }
              }
           //Case 4b: current in HT training symbol. non HT will not come here since it went in previous if or if the previous ifis not true this will be not true        
          else if (current >=plcpHtTrainingSymbolsStart)
             {
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              headerMode),
                                                  plcpHtTrainingSymbolsStart - plcpHsigHeaderStart,
                                                  headerMode);
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                MfHeaderMode),
                                                   plcpHsigHeaderStart - previous,
                                                   MfHeaderMode);
              }
          //Case 4c: current in H sig.non HT will not come here since it went in previous if or if the previous ifis not true this will be not true
          else if (current >=plcpHsigHeaderStart)
             {
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                  current - plcpHsigHeaderStart,
                                                  headerMode);
                 psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                 noiseInterferenceW,
                                                                 MfHeaderMode),
                                                   plcpHsigHeaderStart - previous,
                                                   MfHeaderMode);

             }
         //Case 4d: Current with prev in L SIG
         else 
            {
                //Case 4di: Non HT format (No HT-SIG or Training Symbols)
              if (preamble == WIFI_PREAMBLE_LONG || preamble == WIFI_PREAMBLE_SHORT) //plcpHtTrainingSymbolsStart==plcpHeaderStart)
                {
                    psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              headerMode),
                                                current - previous,
                                                headerMode);
                }
               else
                {
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                               noiseInterferenceW,
                                                               MfHeaderMode),
                                                 current - previous,
                                                 MfHeaderMode);
                }
            }
        }
      //Case 5: previous is in the preamble works for all cases
      else
        {
          if (current >= plcpPayloadStart)
            {
              //for all
              psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                              payloadMode),
                                                current - plcpPayloadStart,
                                                payloadMode); 
             
               // Non HT format (No HT-SIG or Training Symbols)
              if (preamble == WIFI_PREAMBLE_LONG || preamble == WIFI_PREAMBLE_SHORT)
                 psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                 noiseInterferenceW,
                                                                  headerMode),
                                                    plcpPayloadStart - plcpHeaderStart,
                                                    headerMode);
              else
              // Greenfield or Mixed format
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                  plcpHtTrainingSymbolsStart - plcpHsigHeaderStart,
                                                  headerMode);
              if (preamble == WIFI_PREAMBLE_HT_MF)
                 psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                 noiseInterferenceW,
                                                                 MfHeaderMode),
                                                   plcpHsigHeaderStart-plcpHeaderStart,
                                                   MfHeaderMode);             
            }
          else if (current >=plcpHtTrainingSymbolsStart )
          { 
              // Non HT format will not come here since it will execute prev if
              // Greenfield or Mixed format
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                  plcpHtTrainingSymbolsStart - plcpHsigHeaderStart,
                                                  headerMode);
              if (preamble == WIFI_PREAMBLE_HT_MF)
                 psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                 noiseInterferenceW,
                                                                 MfHeaderMode),
                                                   plcpHsigHeaderStart-plcpHeaderStart,
                                                   MfHeaderMode);       
           }
          //non HT will not come here     
          else if (current >=plcpHsigHeaderStart)
             { 
                psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                noiseInterferenceW,
                                                                headerMode),
                                                  current- plcpHsigHeaderStart,
                                                  headerMode); 
                if  (preamble != WIFI_PREAMBLE_HT_GF)
                 {
                   psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                   noiseInterferenceW,
                                                                   MfHeaderMode),
                                                     plcpHsigHeaderStart-plcpHeaderStart,
                                                     MfHeaderMode);    
                  }          
             }
          // GF will not come here
          else if (current >= plcpHeaderStart)
            {
               if (preamble == WIFI_PREAMBLE_LONG || preamble == WIFI_PREAMBLE_SHORT)
                 {
                 psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                                 noiseInterferenceW,
                                                                  headerMode),
                                                    current - plcpHeaderStart,
                                                    headerMode);
                 }
              else
                 {
              psr *= CalculateChunkSuccessRate (CalculateSnr (powerW,
                                                              noiseInterferenceW,
                                                             MfHeaderMode),
                                               current - plcpHeaderStart,
                                               MfHeaderMode);
                       }
            }
        }

      noiseInterferenceW += (*j).GetDelta (m_antennaMode);
      previous = (*j).GetTime ();
      j++;
    }

  double per = 1 - psr;
  return per;
}


struct InterferenceHelper::SnrPer
InterferenceHelper::CalculateSnrPer (Ptr<InterferenceHelper::Event> event)
{
  NS_LOG_DEBUG ("*******************************************");
  NS_LOG_DEBUG ("firstPowerr=" << m_firstPower);
  NS_LOG_DEBUG ("event rxpower=" << event->GetRxPowerW (m_antennaMode) <<
                ",start time=" << event->GetStartTime () <<
                ",end time=" << event->GetEndTime ());
  NS_LOG_DEBUG ("*******************************************");
  for (NiChanges::iterator i = m_niChanges.begin (); i != m_niChanges.end (); i++)
    {
      NS_LOG_DEBUG ("time=" << i->GetTime() << ", deleta=" << i->GetDelta (m_antennaMode));
    }
  NS_LOG_DEBUG ("*******************************************");

  // separate
  Time start = event->GetStartTime();
  Time end = event->GetEndTime();
  
  NiChanges::iterator startIterator = GetEventPosition (event);
  NiChanges::iterator endIterator = GetEventEndPosition (event);

  std::vector<Time> Vtime;
  std::vector<double*> Vdb;

  for (NiChanges::iterator i = m_niChanges.begin (); i != endIterator; i++)
    {
      if(i == startIterator || i == endIterator){continue;}
      if(i->GetDelta (0) < 0){continue;}

      NiChanges::iterator j = i;
      for (; j != m_niChanges.end (); j++)
        {
          NS_LOG_DEBUG ("[i]: "<< i->GetTime () << " [j]: " << j->GetTime ());
          int k;
          for(k = 0; k < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; k++){
            NS_LOG_DEBUG ("[i]: "<< i->GetDelta (k) << " [j]: " << -j->GetDelta (k));
            if(i->GetDelta (k) != -j->GetDelta (k))
              {
                break;
              }
          }
          if(k == WifiAntennaModel::NUMBER_OF_ANTENNA_MODES){
            NS_LOG_DEBUG ("[i]: "<< i->GetTime () << " [j]: " << j->GetTime ());
            break;
          }
        }

      NS_LOG_INFO ("i: " << i->GetTime () << ", start: " << startIterator->GetTime () <<
        "j: " << j->GetTime () << ", end: " << endIterator->GetTime ());
      if(i->GetTime () >= startIterator->GetTime () && i->GetTime () < endIterator->GetTime () && j->GetTime () > endIterator->GetTime ()){
        NS_LOG_DEBUG("[1]i: " << i->GetDelta (m_antennaMode) << " j:" << j->GetDelta (m_antennaMode));
        //        AddNiChangeEvent (NiChange (end - NanoSeconds (1), j->GetDelta ()));
        //        AddNiChangeEvent (NiChange (end + NanoSeconds (1), i->GetDelta ()));
        double *rxPowerDbmi = new double[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
        double *rxPowerDbmj = new double[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
        for(int k = 0; k < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; k++){
          rxPowerDbmi [k] = i->GetDelta (k);
          rxPowerDbmj [k] = j->GetDelta (k);
        }
        Vtime.push_back (end - NanoSeconds (1));
        Vdb.push_back (rxPowerDbmj);
        Vtime.push_back (end + NanoSeconds (1));
        Vdb.push_back (rxPowerDbmi);
      }else if(i->GetTime () <= startIterator->GetTime () && j->GetTime () > endIterator->GetTime ()){
        NS_LOG_DEBUG("[2]i: " << i->GetDelta (m_antennaMode) << " j:" << j->GetDelta (m_antennaMode));
        double *rxPowerDbmi = new double[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
        double *rxPowerDbmj = new double[WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
        for(int k = 0; k < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; k++){
          rxPowerDbmi [k] = i->GetDelta (k);
          rxPowerDbmj [k] = j->GetDelta (k);
        }
        Vtime.push_back (end + NanoSeconds (1));
        Vdb.push_back (rxPowerDbmi);
        Vtime.push_back (end - NanoSeconds (1));
        Vdb.push_back (rxPowerDbmj);
      }
    }

  for (unsigned int i = 0; i < Vtime.size (); i++){
    AddNiChangeEvent (NiChange (Vtime [i], Vdb [i]));
  }

  // sum noise
  NiChanges::iterator nowIterator = GetPosition (start);
  if(m_niChanges.begin() != nowIterator)
    {
      nowIterator--;
    }
  for (NiChanges::iterator i = m_niChanges.begin (); i != nowIterator; i++)
    {
      NS_LOG_DEBUG("start " << i->GetTime() << " GetDelta ()" << i->GetDelta (m_antennaMode));
      m_firstPower += i->GetDelta (m_antennaMode);
    }
  m_niChanges.erase (m_niChanges.begin (), nowIterator);

  NS_LOG_DEBUG ("firstPower=" << m_firstPower);
  for (NiChanges::iterator i = m_niChanges.begin (); i != m_niChanges.end (); i++)
    {
      NS_LOG_DEBUG ("deleta=" << i->GetDelta (m_antennaMode));
    }

  NiChanges ni;
  double noiseInterferenceW = CalculateNoiseInterferenceW (event, &ni);
  NS_LOG_DEBUG ("firstPower=" << m_firstPower);
  for (NiChanges::iterator i = m_niChanges.begin (); i != m_niChanges.end (); i++)
    {
      NS_LOG_DEBUG ("[deleta]=" << i->GetDelta (m_antennaMode));
    }
  double snr = CalculateSnr (event->GetRxPowerW (m_antennaMode),
                             noiseInterferenceW,
                             event->GetPayloadMode ());

  /* calculate the SNIR at the start of the packet and accumulate
   * all SNIR changes in the snir vector.
   */
  double per = CalculatePer (event, &ni);

  // sum noise
  NS_LOG_DEBUG("[firstPower] "<< m_firstPower);
  endIterator = GetEventEndPosition (event);
  endIterator += 1;
  for (NiChanges::iterator i = m_niChanges.begin (); i != endIterator; i++)
    {
      NS_LOG_DEBUG("start " << i->GetTime() << " GetDelta ()" << i->GetDelta (m_antennaMode));
      m_firstPower += i->GetDelta (m_antennaMode);
    }
  m_niChanges.erase (m_niChanges.begin (), endIterator);
  NS_LOG_DEBUG ("#####################################");
  NS_LOG_DEBUG ("firstPower=" << m_firstPower);
  for (NiChanges::iterator i = m_niChanges.begin (); i != m_niChanges.end (); i++)
    {
      for(int m = 0 ; m < 5; m++){
        NS_LOG_DEBUG ("deleta=" << i->GetDelta (m));
      }
    }
  NS_LOG_DEBUG ("#####################################");

  struct SnrPer snrPer;
  snrPer.snr = snr;
  snrPer.per = per;
  return snrPer;
}

void
InterferenceHelper::EraseEvents (void)
{
  m_niChanges.clear ();
  m_rxing = false;
  m_firstPower = 0.0;
}
InterferenceHelper::NiChanges::iterator
InterferenceHelper::GetPosition (Time moment)
{
  double zero [WifiAntennaModel::NUMBER_OF_ANTENNA_MODES];
  for(int i = 0; i <WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; i++){
    zero[i] = 0;
  }
  return std::upper_bound (m_niChanges.begin (), m_niChanges.end (), NiChange (moment, zero));
}

InterferenceHelper::NiChanges::iterator
InterferenceHelper::GetEventPosition (Ptr<InterferenceHelper::Event> event)
{
  NiChanges::iterator i;
  for (i = m_niChanges.begin (); i != m_niChanges.end (); i++){
    if (event->GetStartTime () != i->GetTime ()){
      continue;
    }
    int j;
    for(j = 0; j < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; j++){
      if(i->GetDelta (j) >= Seconds(0) &&
         event->GetRxPowerW (j) != i->GetDelta (j))
        {
          break;
        }
    }
    if(j == WifiAntennaModel::NUMBER_OF_ANTENNA_MODES){
      NS_LOG_DEBUG("[mark]" << i->GetDelta (m_antennaMode));
      return i;
    }
  }
  return i;
}
InterferenceHelper::NiChanges::iterator
InterferenceHelper::GetEventEndPosition (Ptr<InterferenceHelper::Event> event)
{
  NiChanges::iterator i;
  for (i = m_niChanges.begin (); i != m_niChanges.end (); i++){
    if (event->GetEndTime () != i->GetTime ()){
      continue;
    }
    int j;
    for(j = 0; j < WifiAntennaModel::NUMBER_OF_ANTENNA_MODES; j++){
      if(i->GetDelta (j) <= Seconds(0) &&
         -event->GetRxPowerW (j) != i->GetDelta (j))
        {
          break;
        }
    }
    if (j == WifiAntennaModel::NUMBER_OF_ANTENNA_MODES){
      NS_LOG_DEBUG("[mark2]" <<i->GetTime () << i->GetDelta (m_antennaMode));
      return i;
    }

  }
  return i;
}

void
InterferenceHelper::AddNiChangeEvent (NiChange change)
{
  m_niChanges.insert (GetPosition (change.GetTime ()), change);
}
void
InterferenceHelper::NotifyRxStart ()
{
  m_rxing = true;
}
void
InterferenceHelper::NotifyRxEnd ()
{
  m_rxing = false;
}

void
InterferenceHelper::SetupAntennaListener (Ptr<WifiAntennaModel> antenna)
{
  NS_LOG_FUNCTION (this << antenna);
  if (m_antennaListener != 0)
    {
      delete m_antennaListener;
    }
  m_antennaListener = new AntennaListenerInterferenceHelper (this);
  antenna->RegisterListener (m_antennaListener);
}
void
InterferenceHelper::NotifyChangeAntennaModeNow (int mode)
{
  NS_LOG_FUNCTION (this << mode);
  m_antennaMode = mode;
}

} // namespace ns3
