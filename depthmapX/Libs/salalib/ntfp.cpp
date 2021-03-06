// sala - a component of the depthmapX - spatial network analysis platform
// Copyright (C) 2011-2012, Tasos Varoudis

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


// Quick OS land-line NTF parser

#include <iostream>
#include <fstream>

using namespace std;

#include <generic/paftl.h>
#include <generic/p2dpoly.h>
#include <generic/comm.h> // for communicator

#include "ntfp.h"

///////////////////////////////////////////////////////////////////////////////

int NtfPoint::parse(const pstring& token, bool secondhalf /* = false */)
{
   if (secondhalf) {
      pstring second = token.substr(0,m_chars);
      b = second.c_int();
      if (m_chars == 5) {
         b *= 100;
      }
      return 2;
   }
   else if ((int)token.length() < m_chars * 2) {
      if ((int)token.length() < m_chars) {
         return 0;
      }
      pstring first = token.substr(0,m_chars);
      a = first.c_int();
      if (m_chars == 5) {
         a *= 100;
      }
      return 1;
   }
   else {
      pstring first = token.substr(0,m_chars);
      pstring second = token.substr(m_chars,m_chars);
      a = first.c_int();
      b = second.c_int();
      if (m_chars == 5) {
         a *= 100;
         b *= 100;
      }
   }
   return 2;
}

void NtfMap::fitBounds(const Line& li)
{
   if (m_region.isNull()) {
      m_region = li;
   }
   else {
      m_region = runion(m_region,li);
   }
}

void NtfMap::addGeom(int layer, NtfGeometry& geom)
{ 
   m_line_count += geom.size();
   at(layer).m_line_count += geom.size();
   at(layer).push_back( geom );
   for (size_t i = 0; i < geom.size(); i++) {
      fitBounds(geom[i]);
   }
   geom.clear();
}

///////////////////////////////////////////////////////////////////////////////

Line NtfMap::makeLine(const NtfPoint& a, const NtfPoint& b)
{
   // In future requires offset
   return Line(
      Point2f(double(m_offset.a)+double(a.a)/100.0,double(m_offset.b)+double(a.b)/100.0),
      Point2f(double(m_offset.a)+double(b.a)/100.0,double(m_offset.b)+double(b.b)/100.0)
   );
}

// Quick mod - TV
#if defined(_WIN32)
void NtfMap::open(const pqvector<wstring>& fileset, Communicator *comm)
#else
void NtfMap::open(const pqvector<string>& fileset, Communicator *comm)
#endif
{
   // Quick mod - TV
#if defined(_WIN32)   
   __time64_t time = 0;
#else
   time_t time = 0;
#endif   
   qtimer( time, 0 );

   pvecint featcodes;
/*
   m_bottom_left.a =  2147483647;   // 2^31 - 1
   m_bottom_left.b =  2147483647;
   m_top_right.a   = -2147483647;
   m_top_right.b   = -2147483647;
*/
   m_line_count = 0;
   if (size()) {
      clear();
   }

   for (size_t i = 0; i < fileset.size(); i++) {

      ifstream stream(fileset[i].c_str());

      int filetype = NTF_UNKNOWN;

      while (!stream.eof() && filetype == NTF_UNKNOWN) {
         pstring line;
         stream >> line;
         if (line.length() > 2) {
            if (compare(line, "02", 2)) {
               if (compare(line, "02Land-Line", 11)) {
                  filetype = NTF_LANDLINE;
               }
               else if (compare(line, "02Meridian", 10)) {
                  filetype = NTF_MERIDIAN;
               }
            }
         }
      }

      int precision = 10;

      if (filetype == NTF_UNKNOWN) {
         // not recognised -- really ought to throw error
         stream.close();
         continue;
      }
      else if (filetype == NTF_LANDLINE) {
         if (featcodes.add(1) != -1) 
            push_back( NtfLayer("Building outline") );
         if (featcodes.add(4) != -1)
            push_back( NtfLayer("Building outline (overhead)") );
         if (featcodes.add(21) != -1)
            push_back( NtfLayer("Road (public) edge") );
         if (featcodes.add(30) != -1)
            push_back( NtfLayer("General line / minor building") );
         if (featcodes.add(32) != -1)
            push_back( NtfLayer("General ground level / minor o'head detail") );
         if (featcodes.add(52) != -1)
            push_back( NtfLayer("Minor detail") );
         if (featcodes.add(98) != -1)
            push_back( NtfLayer("Road centreline") );
         precision = 6;
      }
      else if (filetype == NTF_MERIDIAN) {
         if (featcodes.add(3000) != -1)
            push_back( NtfLayer("Motorway") );
         if (featcodes.add(3001) != -1) 
            push_back( NtfLayer("A-Road") );
         if (featcodes.add(3002) != -1)
            push_back( NtfLayer("B-Road") );
         if (featcodes.add(3004) != -1)
            push_back( NtfLayer("Minor Road") );
         precision = 5;
      }

      NtfGeometry geom;
      NtfPoint lastpoint(precision), currpoint(precision);
      int parsing = 0;
      int currpos = -1;
      int currtoken = 0;
      pvecstring tokens;

      while (!stream.eof())
      {
         pstring line;
         stream >> line;

         if (line.length()) {
            if (parsing == 0 && compare(line, "07", 2)) {
               // Grab the easting and northing offset
               pstring easting = line.substr(46,10);
               pstring northing =line.substr(56,10);
               m_offset.a = easting.c_int();
               m_offset.b = northing.c_int();
            }
            if (parsing == 0 && compare(line, "23", 2)) {
               geom.clear();
               // In Landline, check to see if it's a code we recognise:
               if (filetype == NTF_LANDLINE) {
                  pstring featcodestr = line.substr(16,4);
                  size_t pos = featcodes.searchindex( featcodestr.c_int() );
                  if (pos != paftl::npos) {
                     at(pos).push_back( NtfGeometry() );
                     parsing = 1;
                     currpos = pos;
                  }
               }
               else if (filetype == NTF_MERIDIAN) {
                  // In Meridian, irritatingly the feature code *follows* the geometry,
                  // just have to read in
                  parsing = 1;
               }
            }
            else if (parsing == 1) {
               if (compare(line, "21", 2)) {
                  tokens.clear();
                  // Some line data:
                  // read to end, and possibly leave hanging:
                  tokens = line.tokenize(' ',true);
                  tokens[0] = tokens[0].substr(13);
                  lastpoint.parse(tokens[0]);
                  currtoken = 1;
                  parsing = 3;
               }
            }
            else if (parsing > 1) {
               if (compare(line, "00", 2)) {
                  tokens = line.tokenize(' ',true);
                  tokens[0] = tokens[0].substr(2);
                  currtoken = 0;
               }
               else if (compare(line, "14", 2) && filetype == NTF_MERIDIAN) {
                  // Meridian record for this line:
                  // finish up and add if featcode is recognised
                  // (goodness knows how we are supposed to know in advance what sort of feature we are given)
                  if (line.length() > 25 && line.substr(23,2) == "FC") { 
                     pstring featcodestr = line.substr(25,4);
                     size_t pos = featcodes.searchindex( featcodestr.c_int() );
                     if (pos != paftl::npos) {
                        addGeom(pos,geom);
                     }
                  }
                  parsing = 0;
               }
            }
            if (parsing > 1) {
               if (parsing == 2) {  // hanging half point:
                  currpoint.parse(tokens[0], true);
                  Line li = makeLine(lastpoint, currpoint);
                  geom.push_back(li);
                  lastpoint = currpoint;
                  currtoken = 1;
               }
               for (size_t i = currtoken; i < tokens.size(); i++) {
                  int numbersparsed = currpoint.parse(tokens[i]);
                  if (numbersparsed == 2) {
                     Line li = makeLine(lastpoint, currpoint);
                     geom.push_back(li);
                     lastpoint = currpoint;
                  }
                  else if (numbersparsed == 1) {
                     parsing = 2; // hanging half point
                  }
                  else {
                     parsing = 3;
                  }
               }
               if (tokens.tail()[tokens.tail().length()-2] == '0') { // 0 here indicates no continuation
                  if (filetype == NTF_LANDLINE) {
                     addGeom(currpos,geom);
                     parsing = 0;
                  }
               }
            }
         }
         if (comm)
         {
            if (qtimer( time, 500 )) {
               if (comm->IsCancelled()) {
                  throw Communicator::CancelledException();
               }
            }
         }
      }
   }
}
