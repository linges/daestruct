/*
 * Copyright (C) 2014 uebb.tu-berlin.de.
 *
 * This file is part of daestruct
 *
 * daestruct is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * daestruct is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with daestruct. If not, see <http://www.gnu.org/licenses/>.
 */

/*
  original implementation by: Roy Jonker @ MagicLogic Optimization Inc.

  Code for Linear Assignment Problem, according to 
   
  "A Shortest Augmenting Path Algorithm for Dense and Sparse Linear   
  Assignment Problems," Computing 38, 325-340, 1987
   
  by   
  R. Jonker and A. Volgenant, University of Amsterdam.

  CHANGED 2004-08-13 by Harold Cooper (hbc@mit.edu) for the pyLAPJV Python module:
  -- commented out system.h and checklap()   

  -- 2014: C++11 port by Christoph Höger 

*/

#include <cstring> //memset
#include <iostream>
#include <algorithm>
#include <boost/heap/d_ary_heap.hpp>
#include <boost/timer/timer.hpp>
#include "lap.hpp"
#include "prettyprint.hpp"

/**
 * comparing nodes based on a distance-map 
 */
struct node_compare {
  std::vector<int>* dist;

  node_compare(std::vector<int>* d) : dist(d) {};

  /* this is a 'greater than' because boost implements max-heaps */
  bool operator()(const int& n1, const int& n2) const { 
    return dist->at(n1) > dist->at(n2);
  }
  
};


/* our priority queue, lent from boost with our custom comparator */
typedef boost::heap::d_ary_heap<int,  boost::heap::arity<4>, boost::heap::mutable_<true>, boost::heap::compare<node_compare>> priority_queue;

struct augmentation_data {
  /* is a column in "todo"? */
  std::vector<bool> in_todo;
  
  /* vector of 'ready' columns */
  std::vector<int> ready;
  std::vector<bool> is_ready;

  /* vector of previous rows for each column in the augmenting path */
  std::vector<int> prev;

  /* scan vector */
  std::vector<int> scan;
  
  /* comparator implementation, based on distances */
  std::vector<int> dist;

  /* priority queue */
  std::vector<priority_queue::handle_type> handles;
  node_compare cmp;
  priority_queue pq;

  augmentation_data(int dim) : in_todo(dim), is_ready(dim), prev(dim), dist(dim), handles(dim), cmp(&dist), pq(cmp) {
    pq.reserve(dim);
  }

  void reset(int start) {
    std::fill(is_ready.begin(), is_ready.end(), false);
    std::fill(in_todo.begin(), in_todo.end(), false);
    std::fill(prev.begin(), prev.end(), start);

    pq.clear();
    ready.clear();
    scan.clear();
  }
};

inline void augment(augmentation_data& data, const daestruct::sigma_matrix& assigncost, std::vector<int>& v, const int start, std::vector<int>& rowsol, std::vector<int>& colsol) {
  data.reset(start);

  auto start_row = assigncost.findRow(start);

  /* iterate twice to get correct order in queue */
  for (auto col = start_row.begin(); col != start_row.end() ; col++)
    data.dist[col.index2()] = *col - v[col.index2()];

  for (auto col = start_row.begin(); col != start_row.end() ; col++) {
    data.handles[col.index2()] = data.pq.push(col.index2());
    data.in_todo[col.index2()] = true;
  }  

  int endofpath = -1;
  int min = 0;
  int iter = 0;
  do {
    iter++;
    if (data.scan.empty()) {
      while (!data.pq.empty() && !data.in_todo[data.pq.top()])
	data.pq.pop();

      min = data.dist[data.pq.top()];
      while(!data.pq.empty() && data.dist[data.pq.top()] == min) {
	const int j = data.pq.top();
	if (data.in_todo[j]) {
	  if (colsol[j] < 0) {
	    endofpath = j;
	    goto augment;
	  }
	  data.scan.push_back(j);
	  data.in_todo[j] = false;
	  data.pq.pop();
	}
      }
    }

    const int j1 = data.scan.back();
    data.scan.pop_back();
    const int i = colsol[j1];
    data.ready.push_back(j1);
    data.is_ready[j1] = true;

    auto row = assigncost.findRow(i);
    const int h = assigncost(i, j1) - v[j1];
    //sparse version of: forall j in TODO
    for (auto col = row.begin(); col != row.end() ; col++) {
      const int j = col.index2();
      if (data.is_ready[j])
	continue;

      const int c_red = *col - v[j] - h;
      if (!data.in_todo[j] || min + c_red < data.dist[j]) {
	data.dist[j] = min + c_red;
	data.prev[j] = i;
	
	if (0 == c_red) {
	  if (colsol[j] < 0) {
	    endofpath = j;
	    goto augment;
	  } else {
	    data.scan.push_back(j);
	    data.in_todo[j] = false;	    
	  }
	} else {
	  if (!data.in_todo[j]) {
	    data.handles[j] = data.pq.push(j);
	    data.in_todo[j] = true;
	  } else {
	    data.pq.update(data.handles[j]);
	  }
	}
      }      
    }

  } while(true);
  
 augment:
  /* update column prices */
  for (const int j : data.ready) {
    v[j] = v[j] + data.dist[j] - min;
  }
  
  /* augment solution */
  int i;
  do {
    i = data.prev[endofpath]; 
    colsol[endofpath] = i; 
    const int j1 = endofpath; 
    endofpath = rowsol[i]; 
    rowsol[i] = j1;
  }
  while(i != start);
}

std::ostream& operator<<(std::ostream& o, const solution& s) {
  o << "solution " << 
    "{ cost=" << s.cost << 
    ", rowsol=" << s.rowsol << 
    ", colsol=" << s.colsol <<
    ", u=" << s.u <<
    ", v=" << s.v << "}";
  return o;
}

solution delta_lap(const daestruct::sigma_matrix& assigncost, const std::vector<int>& _u, const std::vector<int>& _v,
		   const std::vector<int>& _rowsol, const std::vector<int>& _colsol) {
  boost::timer::auto_cpu_timer t;
  const long dim = assigncost.dimension;
  std::vector<int> u(dim),v(dim);

  int numfree = 0;
  int* free = new int[dim];       // list of unassigned rows.
  int* d = new int[dim];         // 'cost-distance' in augmenting path calculation.
  int* pred = new int[dim];       // row-predecessor of column in augmenting/alternating path.

  std::vector<int> colsol(_colsol), rowsol(_rowsol); //TODO avoid this copying?

  for (int j = 0; j < dim; j++) {
    const int i = colsol[j];
    if (i >= 0) {
      v[j] = _v[j];
    } else {
      v[j] = BIG;
      for (int i2 = 0; i2 < dim; i2++)
	v[j] = std::min(v[j], assigncost(i2, j) - _u[i2]);
    }
  }

  for (int i = 0; i < _rowsol.size(); i++)
    if (_rowsol[i] < 0) 
      free[numfree++] = i;

  /*
  std::cout << "Running delta analysis on " << numfree << " unassigned rows" << std::endl;
  std::cout << _rowsol << std::endl;
  std::cout << _colsol << std::endl;
  std::cout << u << std::endl;
  std::cout << v << std::endl;  
  */

  // AUGMENT SOLUTION for each free row.
  augmentation_data data(assigncost.dimension);
  for (int f = 0; f < numfree; f++) {
    augment(data, assigncost, v, free[f], rowsol, colsol);
  }

  // calculate optimal cost.
  int lapcost = 0;
  for (unsigned int i = 0; i < rowsol.size(); i++) {
    const int j = rowsol[i];
    //std::cout << "u[" << i << "] = " << "assigncost(" << i << "," << j << ") - v[" << j << "] = " <<  assigncost(i,j) << " - " << v[j] << std::endl;
    u[i] = assigncost(i,j) - v[j];
    lapcost = lapcost + assigncost(i,j); 
  }

  // free reserved memory.
  delete[] pred;
  delete[] free;
  delete[] d;

  solution sol;
  sol.u = std::move(u);
  sol.v = std::move(v);
  sol.rowsol = std::move(rowsol);
  sol.colsol = std::move(colsol);
  sol.cost = lapcost;

  //std::cout << "finished delta assignment" << std::endl;
  return sol;
}

solution lap(const daestruct::sigma_matrix& assigncost) {
  const long dim = assigncost.dimension;
  boost::timer::auto_cpu_timer t;
  
  std::vector<int> u(dim),v(dim),rowsol(dim),colsol(dim);
  
  int  i, imin, numfree = 0, prvnumfree, f, i0, k, *pred, *free;
  int  j, j1, j2=0, *matches;  
  int min=0, h, umin, usubmin, *d;

  free = new int[dim];      // list of unassigned rows.
  matches = new int[dim](); // counts how many times a row could be assigned.
  d = new int[dim];         // 'cost-distance' in augmenting path calculation.
  pred = new int[dim];      // row-predecessor of column in augmenting/alternating path.

  // COLUMN REDUCTION 
  for (j = dim-1; j >= 0; j--)    // reverse order gives better results.
  {
    // find minimum cost over rows.
    imin = assigncost.smallest_cost_row(j);
    v[j] = assigncost(imin,j); 

    if (++matches[imin] == 1) 
    { 
      // init assignment if minimum row assigned for first time.
      rowsol[imin] = j; 
      colsol[j] = imin; 
    }
    else
      colsol[j] = -1;        // row already assigned, column not assigned.
  }

  // REDUCTION TRANSFER
  for (auto row_it = assigncost.rowBegin(); row_it != assigncost.rowEnd(); row_it++) { 
    i = row_it.index1();

    if (matches[i] == 0)     // fill list of unassigned 'free' rows.
      free[numfree++] = i;
    else
      if (matches[i] == 1)   // transfer reduction from rows that are assigned once.
      {
        j1 = rowsol[i]; 
        min = BIG;
        for (auto col_it = row_it.begin(); col_it != row_it.end(); col_it++) {  
	  j = col_it.index2();
          if (j != j1)
	    if (*col_it - v[j] < min)
	      min = *col_it - v[j];
	}
        v[j1] = v[j1] - min;
      }
  }

  // AUGMENTING ROW REDUCTION 
  int loopcnt = 0;           // do-loop to be done twice.
  do
  {
    loopcnt++;

    // scan all free rows.
    // in some cases, a free row may be replaced with another one to be scanned next.
    k = 0; 
    prvnumfree = numfree; 
    numfree = 0;             // start list of rows still free after augmenting row reduction.
    while (k < prvnumfree)
    {
      i = free[k]; 
      auto row_it = assigncost.findRow(i);
      k++;

      // find minimum and second minimum reduced cost over columns.
      auto col_it = row_it.begin();
      j1 = col_it.index2();
      umin =  *col_it - v[j1]; 
      usubmin = BIG;
      for (col_it++; col_it != row_it.end(); col_it++) 
      {
	j = col_it.index2();
        h = *col_it - v[j];
        if (h < usubmin) {
          if (h >= umin) 
          { 
            usubmin = h; 
            j2 = j;
          }
          else 
          { 
            usubmin = umin; 
            umin = h; 
            j2 = j1; 
            j1 = j;
          }
	}
      }

      i0 = colsol[j1];
      if (umin < usubmin) 
        // change the reduction of the minimum column to increase the minimum
        // reduced cost in the row to the subminimum.
        v[j1] = v[j1] - (usubmin - umin);
      else                   // minimum and subminimum equal.
        if (i0 >= 0)         // minimum column j1 is assigned.
        { 
          // swap columns j1 and j2, as j2 may be unassigned.
          j1 = j2; 
          i0 = colsol[j2];
        }

      // (re-)assign i to j1, possibly de-assigning an i0.
      rowsol[i] = j1; 
      colsol[j1] = i;

      if (i0 >= 0)           // minimum column j1 assigned earlier.
      {  if (umin < usubmin) 
          // put in current k, and go back to that k.
          // continue augmenting path i - j1 with i0.
          free[--k] = i0; 
        else 
          // no further augmenting reduction possible.
          // store i0 in list of free rows for next phase.
          free[numfree++] = i0; 
      }
    }
  }
  while (loopcnt < 2);       // repeat once.

  t.report();
  std::cout << "Done LAP initialization. " << numfree << " unassigned rows remaining." << std::endl;
  
  // AUGMENT SOLUTION for each free row.
  augmentation_data data(assigncost.dimension);
  for (f = 0; f < numfree; f++) {
    augment(data, assigncost, v, free[f], rowsol, colsol);
  }

  // calculate optimal cost.
  int lapcost = 0;
  for (unsigned int i = 0; i < rowsol.size(); i++) {
    j = rowsol[i];
    u[i] = assigncost(i,j) - v[j];
    lapcost = lapcost + assigncost(i,j); 
  }

  // free reserved memory.
  delete[] pred;
  delete[] free;
  delete[] matches;
  delete[] d;

  /*
  std::cout << "Analysis done " << std::endl;
  std::cout << rowsol << std::endl;
  std::cout << colsol << std::endl;
  */
  solution sol;
  sol.u = std::move(u);
  sol.v = std::move(v);
  sol.rowsol = std::move(rowsol);
  sol.colsol = std::move(colsol);
  sol.cost = lapcost;

  return sol;
}





