/****************************************************************/
/* Parallel Combinatorial BLAS Library (for Graph Computations) */
/* version 1.6 -------------------------------------------------*/
/* date: 6/15/2017 ---------------------------------------------*/
/* authors: Ariful Azad, Aydin Buluc  --------------------------*/
/****************************************************************/
/*
 Copyright (c) 2010-2017, The Regents of the University of California
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */


#ifndef _PAR_FRIENDS_EXT_H_
#define _PAR_FRIENDS_EXT_H_

#include "mpi.h"
#include <iostream>
#include "SpParMat.h"	
#include "SpParHelper.h"
#include "MPIType.h"
#include "Friends.h"

namespace combblas {

template <class IT, class NT, class DER>
class SpParMat;

/*************************************************************************************************/
/**************************** FRIEND FUNCTIONS FOR PARALLEL CLASSES ******************************/
/************************** EXTENDED SET (NOT COMMONLY USED FUNCTIONS) ***************************/
/*************************************************************************************************/


/**
 * Parallel A = B*C routine that uses one-sided MPI-2 features
 * General active target syncronization via MPI_Win_Post, MPI_Win_Start, MPI_Win_Complete, MPI_Win_Wait
 * Tested on my dual core Macbook with 1,4,9,16,25 MPI processes
 * No memory hog: splits the matrix into two along the column, prefetches the next half matrix while computing on the current one 
 **/  
template <typename SR, typename IU, typename NU1, typename NU2, typename UDERA, typename UDERB> 
SpParMat<IU,typename promote_trait<NU1,NU2>::T_promote,typename promote_trait<UDERA,UDERB>::T_promote> Mult_AnXBn_ActiveTarget 
		(const SpParMat<IU,NU1,UDERA> & A, const SpParMat<IU,NU2,UDERB> & B )

{
	typedef typename promote_trait<NU1,NU2>::T_promote N_promote;
	typedef typename promote_trait<UDERA,UDERB>::T_promote DER_promote;

	if(A.getncol() != B.getnrow())
	{
    std::cout<<"Can not multiply, dimensions does not match"<<std::endl;
		MPI_Abort(MPI_COMM_WORLD, DIMMISMATCH);
		return SpParMat< IU,N_promote,DER_promote >();
	}
	int stages, Aoffset, Boffset; 	// stages = inner dimension of matrix blocks
  std::shared_ptr<CommGrid> GridC = ProductGrid((A.commGrid).get(), (B.commGrid).get(), stages, Aoffset, Boffset);		

	IU C_m = A.spSeq->getnrow();
	IU C_n = B.spSeq->getncol();
		
	UDERA A1seq, A2seq;
	(A.spSeq)->Split( A1seq, A2seq); 
	
	// ABAB: It should be able to perform split/merge with the transpose option [single function call]
	const_cast< UDERB* >(B.spSeq)->Transpose();
	
	UDERB B1seq, B2seq;
	(B.spSeq)->Split( B1seq, B2seq);
	
	// Create row and column windows (collective operation, i.e. everybody exposes its window to others)
  std::vector<MPI_Win> rowwins1, rowwins2, colwins1, colwins2;
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), A1seq, rowwins1);
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), A2seq, rowwins2);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), B1seq, colwins1);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), B2seq, colwins2);

	// ABAB: We can optimize the call to create windows in the absence of passive synchronization
	// 	MPI_Info info; 
	// 	MPI_Info_create ( &info ); 
	// 	MPI_Info_set( info, "no_locks", "true" ); 
	// 	MPI_Win_create( . . ., info, . . . ); 
	// 	MPI_Info_free( &info ); 
	
	IU ** ARecvSizes1 = SpHelper::allocate2D<IU>(UDERA::esscount, stages);
	IU ** ARecvSizes2 = SpHelper::allocate2D<IU>(UDERA::esscount, stages);
	IU ** BRecvSizes1 = SpHelper::allocate2D<IU>(UDERB::esscount, stages);
	IU ** BRecvSizes2 = SpHelper::allocate2D<IU>(UDERB::esscount, stages);
		
	SpParHelper::GetSetSizes( A1seq, ARecvSizes1, (A.commGrid)->GetRowWorld());
	SpParHelper::GetSetSizes( A2seq, ARecvSizes2, (A.commGrid)->GetRowWorld());
	SpParHelper::GetSetSizes( B1seq, BRecvSizes1, (B.commGrid)->GetColWorld());
	SpParHelper::GetSetSizes( B2seq, BRecvSizes2, (B.commGrid)->GetColWorld());
	
	// Remotely fetched matrices are stored as pointers
	UDERA * ARecv1, * ARecv2; 
	UDERB * BRecv1, * BRecv2;
  std::vector< SpTuples<IU,N_promote>  *> tomerge;

	MPI_Group row_group, col_group;
	MPI_Comm_group((A.commGrid)->GetRowWorld(), &row_group);
	MPI_Comm_group((B.commGrid)->GetColWorld(), &col_group);

	int Aself = (A.commGrid)->GetRankInProcRow();
	int Bself = (B.commGrid)->GetRankInProcCol();	

#ifdef SPGEMMDEBUG
	MPI_Barrier(GridC->GetWorld());
	SpParHelper::Print("Writing to file\n");
  std::ofstream oput;
	GridC->OpenDebugFile("deb", oput);
	oput << "A1seq: " << A1seq.getnrow() << " " << A1seq.getncol() << " " << A1seq.getnnz() << std::endl;
	oput << "A2seq: " << A2seq.getnrow() << " " << A2seq.getncol() << " " << A2seq.getnnz() << std::endl;
	oput << "B1seq: " << B1seq.getnrow() << " " << B1seq.getncol() << " " << B1seq.getnnz() << std::endl;
	oput << "B2seq: " << B2seq.getnrow() << " " << B2seq.getncol() << " " << B2seq.getnnz() << std::endl;
	SpParHelper::Print("Wrote to file\n");
	MPI_Barrier(GridC->GetWorld());
#endif
	
	SpParHelper::PostExposureEpoch(Aself, rowwins1, row_group);
	SpParHelper::PostExposureEpoch(Aself, rowwins2, row_group);
	SpParHelper::PostExposureEpoch(Bself, colwins1, col_group);
	SpParHelper::PostExposureEpoch(Bself, colwins2, col_group);
	
	MPI_Barrier(GridC->GetWorld());
	SpParHelper::Print("Exposure epochs posted\n");	
	MPI_Barrier(GridC->GetWorld());
	
	int Aowner = (0+Aoffset) % stages;		
	int Bowner = (0+Boffset) % stages;
	SpParHelper::AccessNFetch(ARecv1, Aowner, rowwins1, row_group, ARecvSizes1);
	SpParHelper::AccessNFetch(ARecv2, Aowner, rowwins2, row_group, ARecvSizes2);	// Start prefetching next half 

	for(int j=0; j< rowwins1.size(); ++j)	// wait for the first half to complete
		rowwins1[j].Complete();
		
	SpParHelper::AccessNFetch(BRecv1, Bowner, colwins1, col_group, BRecvSizes1);
	SpParHelper::AccessNFetch(BRecv2, Bowner, colwins2, col_group, BRecvSizes2);	// Start prefetching next half 
			
	for(int j=0; j< colwins1.size(); ++j)
		colwins1[j].Complete();

	for(int i = 1; i < stages; ++i) 
	{

#ifdef SPGEMMDEBUG
		SpParHelper::Print("Stage starting\n");
#endif

		SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv1, *BRecv1, false, true);

#ifdef SPGEMMDEBUG
		SpParHelper::Print("Multiplied\n");
#endif

		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

#ifdef SPGEMMDEBUG
		SpParHelper::Print("Pushed back\n");
		MPI_Barrier(GridC->GetWorld());	
#endif

		bool remoteA = false;
		bool remoteB = false;

		delete ARecv1;		// free the memory of the previous first half
		for(int j=0; j< rowwins2.size(); ++j)	// wait for the previous second half to complete
			rowwins2[j].Complete();
		SpParHelper::Print("Completed A\n");

		delete BRecv1;
		for(int j=0; j< colwins2.size(); ++j)	// wait for the previous second half to complete
			colwins2[j].Complete();
		
#ifdef SPGEMMDEBUG
		SpParHelper::Print("Completed B\n");
		MPI_Barrier(GridC->GetWorld());
#endif

		Aowner = (i+Aoffset) % stages;		
		Bowner = (i+Boffset) % stages;

		// start fetching the current first half 
		SpParHelper::AccessNFetch(ARecv1, Aowner, rowwins1, row_group, ARecvSizes1);
		SpParHelper::AccessNFetch(BRecv1, Bowner, colwins1, col_group, BRecvSizes1);
	
#ifdef SPGEMMDEBUG
		SpParHelper::Print("Fetched next\n");
		MPI_Barrier(GridC->GetWorld());
#endif
		
		// while multiplying the already completed previous second halfs
		C_cont = MultiplyReturnTuples<SR>(*ARecv2, *BRecv2, false, true);	
		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

#ifdef SPGEMMDEBUG
		SpParHelper::Print("Multiplied and pushed\n");
		MPI_Barrier(GridC->GetWorld());
#endif

		delete ARecv2;		// free memory of the previous second half
		delete BRecv2;

		for(int j=0; j< rowwins1.size(); ++j)	// wait for the current first half to complte
			rowwins1[j].Complete();
		for(int j=0; j< colwins1.size(); ++j)
			colwins1[j].Complete();
		
#ifdef SPGEMMDEBUG
		SpParHelper::Print("Completed next\n");	
		MPI_Barrier(GridC->GetWorld());
#endif

		// start prefetching the current second half 
		SpParHelper::AccessNFetch(ARecv2, Aowner, rowwins2, row_group, ARecvSizes2);
		SpParHelper::AccessNFetch(BRecv2, Bowner, colwins2, col_group, BRecvSizes2);
	}
	SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv1, *BRecv1, false, true);
	if(!C_cont->isZero()) 
		tomerge.push_back(C_cont);

	delete ARecv1;		// free the memory of the previous first half
	for(int j=0; j< rowwins2.size(); ++j)	// wait for the previous second half to complete
		rowwins2[j].Complete();
	delete BRecv1;
	for(int j=0; j< colwins2.size(); ++j)	// wait for the previous second half to complete
		colwins2[j].Complete();	

	C_cont = MultiplyReturnTuples<SR>(*ARecv2, *BRecv2, false, true);	
	if(!C_cont->isZero()) 
		tomerge.push_back(C_cont);
		
	delete ARecv2;
	delete BRecv2;

	SpHelper::deallocate2D(ARecvSizes1, UDERA::esscount);
	SpHelper::deallocate2D(ARecvSizes2, UDERA::esscount);
	SpHelper::deallocate2D(BRecvSizes1, UDERB::esscount);
	SpHelper::deallocate2D(BRecvSizes2, UDERB::esscount);
			
	DER_promote * C = new DER_promote(MergeAll<SR>(tomerge, C_m, C_n), false, NULL);	// First get the result in SpTuples, then convert to UDER
	for(int i=0; i<tomerge.size(); ++i)
	{
		delete tomerge[i];
	}

	// MPI_Win_Wait() works like a barrier as it waits for all origins to finish their remote memory operation on "this" window
	SpParHelper::WaitNFree(rowwins1);
	SpParHelper::WaitNFree(rowwins2);
	SpParHelper::WaitNFree(colwins1);
	SpParHelper::WaitNFree(colwins2);	
	
	(A.spSeq)->Merge(A1seq, A2seq);
	(B.spSeq)->Merge(B1seq, B2seq);	
	
	MPI_Group_free(&row_group);
	MPI_Group_free(&col_group);
	const_cast< UDERB* >(B.spSeq)->Transpose();	// transpose back to original
	return SpParMat<IU,N_promote,DER_promote> (C, GridC);		// return the result object
}

/**
  * Parallel A = B*C routine that uses one-sided MPI-2 features
  * This function implements an asynchronous 2D algorithm, in the sense that there is no notion of stages.
  * \n The process that completes its submatrix update, requests subsequent matrices from their owners w/out waiting to sychronize with other processors
  * \n This partially remedies the severe load balancing problem in sparse matrices. 
  * \n The class uses MPI-2 to achieve one-sided asynchronous communication
  * \n The algorithm treats each submatrix as a single block
  * \n Local data structure can be any SpMat that has a constructor with array sizes and getarrs() member 
  * Passive target syncronization via MPI_Win_Lock, MPI_Win_Unlock
  * No memory hog: splits the matrix into two along the column, prefetches the next half matrix while computing on the current one 
 **/  
template <typename SR, typename IU, typename NU1, typename NU2, typename UDERA, typename UDERB> 
SpParMat<IU,typename promote_trait<NU1,NU2>::T_promote,typename promote_trait<UDERA,UDERB>::T_promote> Mult_AnXBn_PassiveTarget 
		(const SpParMat<IU,NU1,UDERA> & A, const SpParMat<IU,NU2,UDERB> & B )

{
	typedef typename promote_trait<NU1,NU2>::T_promote N_promote;
	typedef typename promote_trait<UDERA,UDERB>::T_promote DER_promote;

	if(A.getncol() != B.getnrow())
	{
    std::cout<<"Can not multiply, dimensions does not match"<<std::endl;
		MPI_Abort(MPI_COMM_WORLD, DIMMISMATCH);
		return SpParMat< IU,N_promote,DER_promote >();
	}
	int stages, Aoffset, Boffset; 	// stages = inner dimension of matrix blocks
  std::shared_ptr<CommGrid> GridC = ProductGrid((A.commGrid).get(), (B.commGrid).get(), stages, Aoffset, Boffset);		

	IU C_m = A.spSeq->getnrow();
	IU C_n = B.spSeq->getncol();

	UDERA A1seq, A2seq;
	(A.spSeq)->Split( A1seq, A2seq); 
	
	// ABAB: It should be able to perform split/merge with the transpose option [single function call]
	const_cast< UDERB* >(B.spSeq)->Transpose();
	
	UDERB B1seq, B2seq;
	(B.spSeq)->Split( B1seq, B2seq);
	
	// Create row and column windows (collective operation, i.e. everybody exposes its window to others)
  std::vector<MPI_Win> rowwins1, rowwins2, colwins1, colwins2;
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), A1seq, rowwins1);
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), A2seq, rowwins2);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), B1seq, colwins1);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), B2seq, colwins2);

	IU ** ARecvSizes1 = SpHelper::allocate2D<IU>(UDERA::esscount, stages);
	IU ** ARecvSizes2 = SpHelper::allocate2D<IU>(UDERA::esscount, stages);
	IU ** BRecvSizes1 = SpHelper::allocate2D<IU>(UDERB::esscount, stages);
	IU ** BRecvSizes2 = SpHelper::allocate2D<IU>(UDERB::esscount, stages);
		
	SpParHelper::GetSetSizes( A1seq, ARecvSizes1, (A.commGrid)->GetRowWorld());
	SpParHelper::GetSetSizes( A2seq, ARecvSizes2, (A.commGrid)->GetRowWorld());
	SpParHelper::GetSetSizes( B1seq, BRecvSizes1, (B.commGrid)->GetColWorld());
	SpParHelper::GetSetSizes( B2seq, BRecvSizes2, (B.commGrid)->GetColWorld());

	// Remotely fetched matrices are stored as pointers
	UDERA * ARecv1, * ARecv2; 
	UDERB * BRecv1, * BRecv2;
  std::vector< SpTuples<IU,N_promote> *> tomerge;	// sorted triples to be merged

	MPI_Group row_group, col_group;
	MPI_Comm_group((A.commGrid)->GetRowWorld(), &row_group);
	MPI_Comm_group((B.commGrid)->GetColWorld(), &col_group);

	int Aself = (A.commGrid)->GetRankInProcRow();
	int Bself = (B.commGrid)->GetRankInProcCol();	
	
	int Aowner = (0+Aoffset) % stages;		
	int Bowner = (0+Boffset) % stages;
	
	SpParHelper::LockNFetch(ARecv1, Aowner, rowwins1, row_group, ARecvSizes1);
	SpParHelper::LockNFetch(ARecv2, Aowner, rowwins2, row_group, ARecvSizes2);	// Start prefetching next half 
	SpParHelper::LockNFetch(BRecv1, Bowner, colwins1, col_group, BRecvSizes1);
	SpParHelper::LockNFetch(BRecv2, Bowner, colwins2, col_group, BRecvSizes2);	// Start prefetching next half 
		
	// Finish the first halfs
	SpParHelper::UnlockWindows(Aowner, rowwins1);
	SpParHelper::UnlockWindows(Bowner, colwins1);

	for(int i = 1; i < stages; ++i) 
	{
		SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv1, *BRecv1, false, true);

		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

		bool remoteA = false;
		bool remoteB = false;

		delete ARecv1;		// free the memory of the previous first half
		delete BRecv1;

		SpParHelper::UnlockWindows(Aowner, rowwins2);	// Finish the second half
		SpParHelper::UnlockWindows(Bowner, colwins2);	

		Aowner = (i+Aoffset) % stages;		
		Bowner = (i+Boffset) % stages;

		// start fetching the current first half 
		SpParHelper::LockNFetch(ARecv1, Aowner, rowwins1, row_group, ARecvSizes1);
		SpParHelper::LockNFetch(BRecv1, Bowner, colwins1, col_group, BRecvSizes1);
	
		// while multiplying the already completed previous second halfs
		C_cont = MultiplyReturnTuples<SR>(*ARecv2, *BRecv2, false, true);	
		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

		delete ARecv2;		// free memory of the previous second half
		delete BRecv2;

		// wait for the current first half to complte
		SpParHelper::UnlockWindows(Aowner, rowwins1);
		SpParHelper::UnlockWindows(Bowner, colwins1);
		
		// start prefetching the current second half 
		SpParHelper::LockNFetch(ARecv2, Aowner, rowwins2, row_group, ARecvSizes2);
		SpParHelper::LockNFetch(BRecv2, Bowner, colwins2, col_group, BRecvSizes2);
	}

	SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv1, *BRecv1, false, true);
	if(!C_cont->isZero()) 
		tomerge.push_back(C_cont);

	delete ARecv1;		// free the memory of the previous first half
	delete BRecv1;
	
	SpParHelper::UnlockWindows(Aowner, rowwins2);
	SpParHelper::UnlockWindows(Bowner, colwins2);

	C_cont = MultiplyReturnTuples<SR>(*ARecv2, *BRecv2, false, true);	
	if(!C_cont->isZero()) 
		tomerge.push_back(C_cont);		
		
	delete ARecv2;
	delete BRecv2;

	SpHelper::deallocate2D(ARecvSizes1, UDERA::esscount);
	SpHelper::deallocate2D(ARecvSizes2, UDERA::esscount);
	SpHelper::deallocate2D(BRecvSizes1, UDERB::esscount);
	SpHelper::deallocate2D(BRecvSizes2, UDERB::esscount);
			
	DER_promote * C = new DER_promote(MergeAll<SR>(tomerge, C_m, C_n), false, NULL);	// First get the result in SpTuples, then convert to UDER
	for(int i=0; i<tomerge.size(); ++i)
	{
		delete tomerge[i];
	}
	
	SpParHelper::FreeWindows(rowwins1);
	SpParHelper::FreeWindows(rowwins2);
	SpParHelper::FreeWindows(colwins1);
	SpParHelper::FreeWindows(colwins2);	

	(A.spSeq)->Merge(A1seq, A2seq);
	(B.spSeq)->Merge(B1seq, B2seq);	

	MPI_Group_free(&row_group);
	MPI_Group_free(&col_group);
	const_cast< UDERB* >(B.spSeq)->Transpose();	// transpose back to original
	return SpParMat<IU,N_promote,DER_promote> (C, GridC);		// return the result object
}

/**
 * Parallel A = B*C routine that uses one-sided MPI-2 features
 * Syncronization is through MPI_Win_Fence
 * Buggy as of September, 2009
 **/ 
template <typename SR, typename IU, typename NU1, typename NU2, typename UDERA, typename UDERB> 
SpParMat<IU,typename promote_trait<NU1,NU2>::T_promote,typename promote_trait<UDERA,UDERB>::T_promote> Mult_AnXBn_Fence
		(const SpParMat<IU,NU1,UDERA> & A, const SpParMat<IU,NU2,UDERB> & B )
{
	typedef typename promote_trait<NU1,NU2>::T_promote N_promote;
	typedef typename promote_trait<UDERA,UDERB>::T_promote DER_promote;
	
	if(A.getncol() != B.getnrow())
	{
    std::cout<<"Can not multiply, dimensions does not match"<<std::endl;
		MPI_Abort(MPI_COMM_WORLD, DIMMISMATCH);
		return SpParMat< IU,N_promote,DER_promote >();
	}

	int stages, Aoffset, Boffset; 	// stages = inner dimension of matrix blocks
  std::shared_ptr<CommGrid> GridC = ProductGrid((A.commGrid).get(), (B.commGrid).get(), stages, Aoffset, Boffset);		
			
  std::ofstream oput;
	GridC->OpenDebugFile("deb", oput);
	const_cast< UDERB* >(B.spSeq)->Transpose();
	
	// set row & col window handles
  std::vector<MPI_Win> rowwindows, colwindows;
  std::vector<MPI_Win> rowwinnext, colwinnext;
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), *(A.spSeq), rowwindows);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), *(B.spSeq), colwindows);
	SpParHelper::SetWindows((A.commGrid)->GetRowWorld(), *(A.spSeq), rowwinnext);
	SpParHelper::SetWindows((B.commGrid)->GetColWorld(), *(B.spSeq), colwinnext);
	
	IU ** ARecvSizes = SpHelper::allocate2D<IU>(UDERA::esscount, stages);
	IU ** BRecvSizes = SpHelper::allocate2D<IU>(UDERB::esscount, stages);
	
	SpParHelper::GetSetSizes( *(A.spSeq), ARecvSizes, (A.commGrid)->GetRowWorld());
	SpParHelper::GetSetSizes( *(B.spSeq), BRecvSizes, (B.commGrid)->GetColWorld());
	
	UDERA * ARecv, * ARecvNext; 
	UDERB * BRecv, * BRecvNext;
  std::vector< SpTuples<IU,N_promote>  *> tomerge;

	// Prefetch first
	for(int j=0; j< rowwindows.size(); ++j)
		MPI_Win_fence(MPI_MODE_NOPRECEDE, rowwindows[j]);
	for(int j=0; j< colwindows.size(); ++j)
		MPI_Win_fence(MPI_MODE_NOPRECEDE, colwindows[j]);

	for(int j=0; j< rowwinnext.size(); ++j)
		MPI_Win_fence(MPI_MODE_NOPRECEDE, rowwinnext[j]);
	for(int j=0; j< colwinnext.size(); ++j)
		MPI_Win_fence(MPI_MODE_NOPRECEDE, colwinnext[j]);


	int Aownind = (0+Aoffset) % stages;		
	int Bownind = (0+Boffset) % stages;
	if(Aownind == (A.commGrid)->GetRankInProcRow())
	{	
		ARecv = A.spSeq;	// shallow-copy 
	}
	else
	{
    std::vector<IU> ess1(UDERA::esscount);		// pack essentials to a vector
		for(int j=0; j< UDERA::esscount; ++j)	
		{
			ess1[j] = ARecvSizes[j][Aownind];	
		}
		ARecv = new UDERA();	// create the object first	

		oput << "For A (out), Fetching " << (void*)rowwindows[0] << std::endl;
		SpParHelper::FetchMatrix(*ARecv, ess1, rowwindows, Aownind);	// fetch its elements later
	}
	if(Bownind == (B.commGrid)->GetRankInProcCol())
	{
		BRecv = B.spSeq;	// shallow-copy
	}
	else
	{
    std::vector<IU> ess2(UDERB::esscount);		// pack essentials to a vector
		for(int j=0; j< UDERB::esscount; ++j)	
		{
			ess2[j] = BRecvSizes[j][Bownind];	
		}	
		BRecv = new UDERB();

		oput << "For B (out), Fetching " << (void*)colwindows[0] << std::endl;
		SpParHelper::FetchMatrix(*BRecv, ess2, colwindows, Bownind);	// No lock version, only get !
	}

	int Aownprev = Aownind;
	int Bownprev = Bownind;
	
	for(int i = 1; i < stages; ++i) 
	{
		Aownind = (i+Aoffset) % stages;		
		Bownind = (i+Boffset) % stages;

		if(i % 2 == 1)	// Fetch RecvNext via winnext, fence on Recv via windows
		{	
			if(Aownind == (A.commGrid)->GetRankInProcRow())
			{	
				ARecvNext = A.spSeq;	// shallow-copy 
			}
			else
			{
        std::vector<IU> ess1(UDERA::esscount);		// pack essentials to a vector
				for(int j=0; j< UDERA::esscount; ++j)	
				{
					ess1[j] = ARecvSizes[j][Aownind];	
				}
				ARecvNext = new UDERA();	// create the object first	

				oput << "For A, Fetching " << (void*) rowwinnext[0] << std::endl;
				SpParHelper::FetchMatrix(*ARecvNext, ess1, rowwinnext, Aownind);
			}
		
			if(Bownind == (B.commGrid)->GetRankInProcCol())
			{
				BRecvNext = B.spSeq;	// shallow-copy
			}
			else
			{
        std::vector<IU> ess2(UDERB::esscount);		// pack essentials to a vector
				for(int j=0; j< UDERB::esscount; ++j)	
				{
					ess2[j] = BRecvSizes[j][Bownind];	
				}		
				BRecvNext = new UDERB();

				oput << "For B, Fetching " << (void*)colwinnext[0] << std::endl;
				SpParHelper::FetchMatrix(*BRecvNext, ess2, colwinnext, Bownind);	// No lock version, only get !
			}
		
			oput << "Fencing " << (void*) rowwindows[0] << std::endl;
			oput << "Fencing " << (void*) colwindows[0] << std::endl;
		
			for(int j=0; j< rowwindows.size(); ++j)
				MPI_Win_fence(MPI_MODE_NOSTORE, rowwindows[j]);		// Synch using "other" windows
			for(int j=0; j< colwindows.size(); ++j)
				MPI_Win_fence(MPI_MODE_NOSTORE, colwindows[j]);
	
			SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv, *BRecv, false, true);
			if(!C_cont->isZero()) 
				tomerge.push_back(C_cont);

			if(Aownprev != (A.commGrid)->GetRankInProcRow()) delete ARecv;
			if(Bownprev != (B.commGrid)->GetRankInProcCol()) delete BRecv;

			Aownprev = Aownind;
			Bownprev = Bownind; 
		}	
		else	// fetch to Recv via windows, fence on RecvNext via winnext
		{	
			
			if(Aownind == (A.commGrid)->GetRankInProcRow())
			{	
				ARecv = A.spSeq;	// shallow-copy 
			}
			else
			{
        std::vector<IU> ess1(UDERA::esscount);		// pack essentials to a vector
				for(int j=0; j< UDERA::esscount; ++j)	
				{
					ess1[j] = ARecvSizes[j][Aownind];	
				}
				ARecv = new UDERA();	// create the object first	

				oput << "For A, Fetching " << (void*) rowwindows[0] << std::endl;
				SpParHelper::FetchMatrix(*ARecv, ess1, rowwindows, Aownind);
			}
		
			if(Bownind == (B.commGrid)->GetRankInProcCol())
			{
				BRecv = B.spSeq;	// shallow-copy
			}
			else
			{
        std::vector<IU> ess2(UDERB::esscount);		// pack essentials to a vector
				for(int j=0; j< UDERB::esscount; ++j)	
				{
					ess2[j] = BRecvSizes[j][Bownind];	
				}		
				BRecv = new UDERB();

				oput << "For B, Fetching " << (void*)colwindows[0] << std::endl;
				SpParHelper::FetchMatrix(*BRecv, ess2, colwindows, Bownind);	// No lock version, only get !
			}
		
			oput << "Fencing " << (void*) rowwinnext[0] << std::endl;
			oput << "Fencing " << (void*) rowwinnext[0] << std::endl;
		
			for(int j=0; j< rowwinnext.size(); ++j)
				MPI_Win_fence(MPI_MODE_NOSTORE, rowwinnext[j]);		// Synch using "other" windows
			for(int j=0; j< colwinnext.size(); ++j)
				MPI_Win_fence(MPI_MODE_NOSTORE, colwinnext[j]);

			SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecvNext, *BRecvNext, false, true);
			if(!C_cont->isZero()) 
				tomerge.push_back(C_cont);


			if(Aownprev != (A.commGrid)->GetRankInProcRow()) delete ARecvNext;
			if(Bownprev != (B.commGrid)->GetRankInProcCol()) delete BRecvNext;

			Aownprev = Aownind;
			Bownprev = Bownind; 
		}

	}

	if(stages % 2 == 1)	// fence on Recv via windows
	{
		oput << "Fencing " << (void*) rowwindows[0] << std::endl;
		oput << "Fencing " << (void*) colwindows[0] << std::endl;

		for(int j=0; j< rowwindows.size(); ++j)
			MPI_Win_fence(MPI_MODE_NOSUCCEED, rowwindows[j]);		// Synch using "prev" windows
		for(int j=0; j< colwindows.size(); ++j)
			MPI_Win_fence(MPI_MODE_NOSUCCEED, colwindows[j]);

		SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecv, *BRecv, false, true);
		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

		if(Aownprev != (A.commGrid)->GetRankInProcRow()) delete ARecv;
		if(Bownprev != (B.commGrid)->GetRankInProcRow()) delete BRecv;
	}
	else		// fence on RecvNext via winnext
	{
		oput << "Fencing " << (void*) rowwinnext[0] << std::endl;
		oput << "Fencing " << (void*) colwinnext[0] << std::endl;

		for(int j=0; j< rowwinnext.size(); ++j)
			MPI_Win_fence(MPI_MODE_NOSUCCEED, rowwinnext[j]);		// Synch using "prev" windows
		for(int j=0; j< colwinnext.size(); ++j)
			MPI_Win_fence(MPI_MODE_NOSUCCEED, colwinnext[j]);

		SpTuples<IU,N_promote> * C_cont = MultiplyReturnTuples<SR>(*ARecvNext, *BRecvNext, false, true);
		if(!C_cont->isZero()) 
			tomerge.push_back(C_cont);

		if(Aownprev != (A.commGrid)->GetRankInProcRow()) delete ARecvNext;
		if(Bownprev != (B.commGrid)->GetRankInProcRow()) delete BRecvNext;
	}
	for(int i=0; i< rowwindows.size(); ++i)
	{
		MPI_Win_free(&rowwindows[i]);
		MPI_Win_free(&rowwinnext[i]);
	}
	for(int i=0; i< colwindows.size(); ++i)
	{
		MPI_Win_free(&colwindows[i]);
		MPI_Win_free(&colwinnext[i]);
	}
	MPI_Barrier(GridC->GetWorld());

	IU C_m = A.spSeq->getnrow();
	IU C_n = B.spSeq->getncol();
	DER_promote * C = new DER_promote(MergeAll<SR>(tomerge, C_m, C_n), false, NULL);	// First get the result in SpTuples, then convert to UDER
	for(int i=0; i<tomerge.size(); ++i)
	{
		delete tomerge[i];
	}
	SpHelper::deallocate2D(ARecvSizes, UDERA::esscount);
	SpHelper::deallocate2D(BRecvSizes, UDERB::esscount);
	
	const_cast< UDERB* >(B.spSeq)->Transpose();	// transpose back to original	
	return SpParMat<IU,N_promote,DER_promote> (C, GridC);			// return the result object
}

}

#endif
