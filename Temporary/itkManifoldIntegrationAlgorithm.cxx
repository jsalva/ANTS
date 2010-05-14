/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit (ITK)
  Module:    $RCSfile: itkManifoldIntegrationAlgorithm.cxx,v $
  Language:  C++
  Date:      $Date: 2006/10/13 19:21:27 $
  Version:   $Revision: 1.3 $

=========================================================================*/
#ifndef _itkManifoldIntegrationAlgorithm_cxx_
#define _itkManifoldIntegrationAlgorithm_cxx_

#include "itkSurfaceMeshCurvature.h"
#include "vtkFeatureEdges.h"
#include "vtkPointLocator.h"
#include "vtkCellLocator.h"
#include "vtkTriangleFilter.h"
#include "vtkCleanPolyData.h"
#include "vtkPolyDataConnectivityFilter.h"

namespace itk {


template<class TGraphSearchNode > 
ManifoldIntegrationAlgorithm<TGraphSearchNode>::ManifoldIntegrationAlgorithm()
{
  m_SurfaceMesh=NULL;
  m_QS = DijkstrasAlgorithmQueue<TGraphSearchNode>::New();
  m_MaxCost=vnl_huge_val(m_MaxCost);
  m_PureDist=true;
}

 
template<class TGraphSearchNode > 
float ManifoldIntegrationAlgorithm<TGraphSearchNode>::dstarUestimate(
	typename TGraphSearchNode::Pointer G)
{
  typedef itk::SurfaceMeshCurvature<TGraphSearchNode,TGraphSearchNode> surfktype;
  typename surfktype::Pointer surfk=surfktype::New();
  surfk->SetSurfacePatch(G);
  surfk->FindNeighborhood();
  float dsu=(float) surfk->dstarUestimate();
  return dsu;
}


template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::InitializeGraph3()
{
  if (!m_SurfaceMesh) return;

  // Construct simple triangles
  vtkTriangleFilter* fltTriangle = vtkTriangleFilter::New();
  fltTriangle->SetInput(m_SurfaceMesh);

  cout << "   converting mesh to triangles " << endl;
  fltTriangle->Update();

  // Clean the data
  vtkCleanPolyData* fltCleaner = vtkCleanPolyData::New();
  fltCleaner->SetInput(fltTriangle->GetOutput());
  fltCleaner->SetTolerance(0);
  fltCleaner->ConvertPolysToLinesOn();
  
  cout << "   cleaning up triangle mesh " << endl;
  fltCleaner->Update();

  // Go through and delete the cells that are of the wrong type
  //m_SurfaceMesh 
  vtkPolyData* clean= fltCleaner->GetOutput();
  for(vtkIdType i = clean->GetNumberOfCells();i > 0;i--)
    {
    if(clean->GetCellType(i-1) != VTK_TRIANGLE)
      clean->DeleteCell(i-1);
    }
  clean->BuildCells();
  m_SurfaceMesh=clean;

  vtkPoints* vtkpoints = m_SurfaceMesh->GetPoints();
  vtkPointData *pd=m_SurfaceMesh->GetPointData();
  int numPoints = vtkpoints->GetNumberOfPoints(); 
  vtkDataArray* scs=pd->GetScalars();
  m_Graph.resize(numPoints);
  for(int i =0; i < numPoints; i++)
  {
     NodeLocationType loc;	
     double* pt = vtkpoints->GetPoint(i); 
  	 typename GraphSearchNode<PixelType,CoordRep,GraphDimension>::Pointer G=
		GraphSearchNode<PixelType,CoordRep,GraphDimension>::New();
  	 G->SetUnVisited();
     G->SetTotalCost(m_MaxCost);
	 G->SetValue(scs->GetTuple1(i),3);
     for (int j=0; j<GraphDimension; j++) loc[j]=pt[j]; 	
	 G->SetLocation(loc);
  	 G->SetPredecessor(NULL);
	 G->m_NumberOfNeighbors=0;
	 G->SetIdentity(i);
	 m_Graph[i]=G;
  }

  std::cout << " allocation of graph done ";

// now loop through the cells to get triangles and also edges
  
  vtkCellArray* vtkcells = m_SurfaceMesh->GetPolys();
  
  vtkIdType npts;
  vtkIdType* pts;
  long i = 0;
  for(vtkcells->InitTraversal(); vtkcells->GetNextCell(npts, pts); )
  {
    m_Graph[pts[0]]->m_NumberOfNeighbors+=2;
    m_Graph[pts[1]]->m_NumberOfNeighbors+=2;
    m_Graph[pts[2]]->m_NumberOfNeighbors+=2;
  }
  
  for(int i =0; i < numPoints; i++)
  {
    m_Graph[i]->m_Neighbors.resize(m_Graph[i]->m_NumberOfNeighbors);
//	std::cout <<" Num Neigh " << i << " is " << m_Graph[i]->m_NumberOfNeighbors << std::endl;
	m_Graph[i]->m_NumberOfNeighbors=0;
  }

  for(vtkcells->InitTraversal(); vtkcells->GetNextCell(npts, pts); )
  {
  m_Graph[pts[0]]->m_Neighbors[ m_Graph[pts[0]]->m_NumberOfNeighbors ]=m_Graph[pts[1]];
  m_Graph[pts[0]]->m_NumberOfNeighbors++;
  m_Graph[pts[0]]->m_Neighbors[ m_Graph[pts[0]]->m_NumberOfNeighbors ]=m_Graph[pts[2]];
  m_Graph[pts[0]]->m_NumberOfNeighbors++;

  m_Graph[pts[1]]->m_Neighbors[ m_Graph[pts[1]]->m_NumberOfNeighbors ]=m_Graph[pts[0]];
  m_Graph[pts[1]]->m_NumberOfNeighbors++;
  m_Graph[pts[1]]->m_Neighbors[ m_Graph[pts[1]]->m_NumberOfNeighbors ]=m_Graph[pts[2]];
  m_Graph[pts[1]]->m_NumberOfNeighbors++;

  m_Graph[pts[2]]->m_Neighbors[ m_Graph[pts[2]]->m_NumberOfNeighbors ]=m_Graph[pts[0]];
  m_Graph[pts[2]]->m_NumberOfNeighbors++;
  m_Graph[pts[2]]->m_Neighbors[ m_Graph[pts[2]]->m_NumberOfNeighbors ]=m_Graph[pts[1]];
  m_Graph[pts[2]]->m_NumberOfNeighbors++;
  }
}


template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::InitializeGraph2()
{
  if (!m_SurfaceMesh) return;
/*
  // Construct simple triangles
  vtkTriangleFilter* fltTriangle = vtkTriangleFilter::New();
  fltTriangle->SetInput(m_SurfaceMesh);

  cout << "   converting mesh to triangles " << endl;
  fltTriangle->Update();

  cout << "  this mesh has " << fltTriangle->GetOutput()->GetNumberOfPoints() << " points" << endl;
  cout << "  this mesh has " << fltTriangle->GetOutput()->GetNumberOfCells() << " cells" << endl;

  // Clean the data
  vtkCleanPolyData* fltCleaner = vtkCleanPolyData::New();
  fltCleaner->SetInput(fltTriangle->GetOutput());
  fltCleaner->SetTolerance(0);
  fltCleaner->ConvertPolysToLinesOn();
  
  cout << "   cleaning up triangle mesh " << endl;
  fltCleaner->Update();

  // Go through and delete the cells that are of the wrong type
  //m_SurfaceMesh 
  vtkPolyData* clean= fltCleaner->GetOutput();
  for(vtkIdType i = clean->GetNumberOfCells();i > 0;i--)
    {
    if(clean->GetCellType(i-1) != VTK_TRIANGLE)
      clean->DeleteCell(i-1);
    }
  clean->BuildCells();
*/

  vtkFeatureEdges* fltEdge = vtkFeatureEdges::New();
  fltEdge->BoundaryEdgesOff();
  fltEdge->FeatureEdgesOff();
  fltEdge->NonManifoldEdgesOff();
  fltEdge->ManifoldEdgesOn();
  fltEdge->ColoringOff();
  fltEdge->SetInput(m_SurfaceMesh);
 

  cout << "   extracting edges from the mesh" << endl;
  fltEdge->Update();

  // Got the new poly data
  vtkPolyData* m_EdgePolys = fltEdge->GetOutput();
  m_EdgePolys->BuildCells();
  m_EdgePolys->BuildLinks();
  
  unsigned int nEdges = m_EdgePolys->GetNumberOfLines();
  cout << "      number of edges (lines) : " << nEdges << endl;
  cout << "      number of cells : " << m_EdgePolys->GetNumberOfCells() << endl;
  cout << "      number if points : " << m_EdgePolys->GetNumberOfPoints() << endl;

  vtkPoints* vtkpoints = m_EdgePolys->GetPoints();
  int numPoints = vtkpoints->GetNumberOfPoints(); 
  m_Graph.resize(numPoints);
  for(int i =0; i < numPoints; i++)
  {
     NodeLocationType loc;	
     double* pt = vtkpoints->GetPoint(i); 
  	 typename GraphSearchNode<PixelType,CoordRep,GraphDimension>::Pointer G=
    		GraphSearchNode<PixelType,CoordRep,GraphDimension>::New();
  	 G->SetUnVisited();
     G->SetTotalCost(m_MaxCost);
     for (int j=0; j<GraphDimension; j++) loc[j]=pt[j]; 	
	 G->SetLocation(loc);
  	 G->SetPredecessor(NULL);
	 G->m_NumberOfNeighbors=0;
	 m_Graph[i]=G;
  }

  std::cout << " allocation of graph done ";

  vtkIdType nPoints = 0; vtkIdType *xPoints = NULL;
  for(unsigned int i=0;i<nEdges;i++)
    {
    // Get the next edge
    m_EdgePolys->GetCellPoints(i, nPoints, xPoints);

    // Place the edge into the Edge structure
    assert(nPoints == 2);
    // Place the edge into the Edge structure
//	std::cout << " nPoints " << nPoints << std::endl;	
//	std::cout << " pt " << xPoints[0] << " connects " << xPoints[1] << std::endl;
    assert(nPoints == 2);
	m_Graph[xPoints[0]]->m_NumberOfNeighbors++;
	}
  
	std::cout << " counting nhood done ";
	// second, resize the vector for each G
	for (int i=0; i<numPoints; i++) 
	{
      m_Graph[i]->m_Neighbors.resize(m_Graph[i]->m_NumberOfNeighbors);
	  m_Graph[i]->m_NumberOfNeighbors=0;
	}

    for(unsigned int i=0;i<nEdges;i++)
    {
    // Get the next edge
    m_EdgePolys->GetCellPoints(i, nPoints, xPoints);
    // Place the edge into the Edge structure
    assert(nPoints == 2);
	m_Graph[xPoints[0]]->m_Neighbors[ m_Graph[xPoints[0]]->m_NumberOfNeighbors ]=m_Graph[xPoints[1]];
	m_Graph[xPoints[0]]->m_NumberOfNeighbors++;
	}

//	vtkPolyDataConnectivityFilter* con = vtkPolyDataConnectivityFilter::New();
//    con->SetExtractionModeToLargestRegion();
//    con->SetInput(m_EdgePolys);
//	m_SurfaceMesh=con->GetOutput();
    m_SurfaceMesh=m_EdgePolys;

}


template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::InitializeGraph()
{
if (!m_SurfaceMesh) return;
	std::cout << " Generate graph from surface mesh " << std::endl;
// get size of the surface mesh

    vtkExtractEdges* edgeex=vtkExtractEdges::New();
    edgeex->SetInput(m_SurfaceMesh);
    edgeex->Update();
    vtkPolyData* edg1=edgeex->GetOutput();
    vtkIdType nedg=edg1->GetNumberOfCells();
    vtkIdType vers = m_SurfaceMesh->GetNumberOfPoints();
    int nfac = m_SurfaceMesh->GetNumberOfPolys(); 
    float g = 0.5 * (2.0 - vers + nedg - nfac);
    std::cout << " Genus " << g << std::endl;
    edg1->BuildCells();


	// now cruise through all edges and add to each node's neighbor list
// first, count the num of edges for each node
//    m_SurfaceMesh=edg1;    

	  vtkPoints* vtkpoints = edg1->GetPoints();
  int numPoints = vtkpoints->GetNumberOfPoints();
  m_Graph.resize(numPoints);
  for(int i =0; i < numPoints; i++)
  {
     NodeLocationType loc;	
     double* pt = vtkpoints->GetPoint(i); 
  	 typename GraphSearchNode<PixelType,CoordRep,GraphDimension>::Pointer G=
		    GraphSearchNode<PixelType,CoordRep,GraphDimension>::New();
  	 G->SetUnVisited();
     G->SetTotalCost(m_MaxCost);
     for (int j=0; j<GraphDimension; j++) loc[j]=pt[j]; 	
	 G->SetLocation(loc);
  	 G->SetPredecessor(NULL);
	 G->m_NumberOfNeighbors=0;
	 m_Graph[i]=G;
  }
  std::cout << " allocation of graph done ";

	std::cout << " begin edg iter ";
    vtkIdType nPoints = 0; 
	vtkIdType *xPoints = NULL;
    for(unsigned int i=0;i<nedg;i++)
    {
    // Get the next edge
    edg1->GetCellPoints(i, nPoints, xPoints);
    // Place the edge into the Edge structure
//	std::cout << " nPoints " << nPoints << std::endl;	
//	std::cout << " pt " << xPoints[0] << " connects " << xPoints[1] << std::endl;
    assert(nPoints == 2);
	m_Graph[xPoints[0]]->m_NumberOfNeighbors++;
	}
  
	std::cout << " counting nhood done ";
	// second, resize the vector for each G
	for (int i=0; i<numPoints; i++) 
	{
      m_Graph[i]->m_Neighbors.resize(m_Graph[i]->m_NumberOfNeighbors);
	  m_Graph[i]->m_NumberOfNeighbors=0;
	}

    for(unsigned int i=0;i<nedg;i++)
    {
    // Get the next edge
    edg1->GetCellPoints(i, nPoints, xPoints);
    // Place the edge into the Edge structure
    assert(nPoints == 2);
	m_Graph[xPoints[0]]->m_Neighbors[ m_Graph[xPoints[0]]->m_NumberOfNeighbors ]=m_Graph[xPoints[1]];
	m_Graph[xPoints[0]]->m_NumberOfNeighbors++;
	}

	m_SurfaceMesh=edg1;
	
    return;

}


template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>
::ConvertGraphBackToMesh()
{// this is a sanity check

}



template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::InitializeQueue()
{
  int n = m_QS->m_SourceNodes.size();
//  GraphIteratorType GraphIterator( m_Graph, m_GraphRegion );
//  GraphIterator.GoToBegin();
//  m_GraphIndex = GraphIterator.GetIndex();
  NodeLocationType loc;
  // make sure the graph contains the right pointers 
  for (int i=0; i<n; i++)
  {
    typename GraphSearchNode<PixelType,CoordRep,GraphDimension>::Pointer G = m_QS->m_SourceNodes[i];
	  G->SetPredecessor(G);
  	m_QS->m_Q.push(G);
  	loc=G->GetLocation();
//  	for (int d=0;d<GraphDimension;d++) m_GraphIndex[d]=loc[d];
//	    m_Graph->SetPixel(m_GraphIndex,G);
  }    
  for (int i=0; i<m_QS->m_SinkNodes.size(); i++)
  {
    typename GraphSearchNode<PixelType,CoordRep,GraphDimension>::Pointer G = m_QS->m_SinkNodes[i];
	  G->SetPredecessor(NULL);
	  loc=G->GetLocation();
//	  for (int d=0;d<GraphDimension;d++) m_GraphIndex[d]=(long)loc[d];
//	    m_Graph->SetPixel(m_GraphIndex,G);
  }   
  m_SearchFinished=false;
}


/** 
*  Compute the local cost using Manhattan distance.
*/
template<class TGraphSearchNode > 
typename ManifoldIntegrationAlgorithm<TGraphSearchNode>::
PixelType ManifoldIntegrationAlgorithm<TGraphSearchNode>::LocalCost() 
{
	if (m_PureDist)
	{	
	  NodeLocationType  dif=m_CurrentNode->GetLocation()-m_NeighborNode->GetLocation();
	  float mag = 0.0;
	  for (int jj=0; jj<GraphDimension;jj++) mag+=dif[jj]*dif[jj];
	  return sqrt(mag);
	}
	else
	{
	NodeLocationType  dif=m_CurrentNode->GetLocation()-m_NeighborNode->GetLocation();
	float dU=//dif.magnitude();
		fabs(m_CurrentNode->GetValue()-m_NeighborNode->GetValue());//*dif.magnitude();
	//std::cout << " dU " << dU << " value " << m_CurrentNode->GetValue() <<  std::endl;
	return dU;
	}
//  return 1.0; // manhattan distance
};

template<class TGraphSearchNode >
bool ManifoldIntegrationAlgorithm<TGraphSearchNode>::TerminationCondition()
{	
  if (!m_QS->m_SinkNodes.empty())
  {
    if (m_NeighborNode == m_QS->m_SinkNodes[0] && !m_SearchFinished  ) 
    {
      std::cout << " FOUND SINK ";
      m_SearchFinished=true;
	  m_NeighborNode->SetTotalCost( m_CurrentCost + LocalCost());
	  m_NeighborNode->SetPredecessor(m_CurrentNode);
  	}
  }
  if (m_CurrentCost>=m_MaxCost) m_SearchFinished=true;
  return m_SearchFinished;
}


template<class TGraphSearchNode >
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::SearchEdgeSet() 
{
  int i=0,j=0;
  for (i = 0; i < m_CurrentNode->m_NumberOfNeighbors; i++)
  {     
  	m_NeighborNode=m_CurrentNode->m_Neighbors[i];
//    	std::cout << " i " << i << " position " << m_NeighborNode->GetLocation() << endl;
    TerminationCondition();
	  if (!m_SearchFinished && m_CurrentNode != m_NeighborNode &&
		    !m_NeighborNode->GetDelivered())
	  {
        m_NewCost = m_CurrentCost + LocalCost();
	      CheckNodeStatus();
	  }
  }
}


template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::CheckNodeStatus() 
// checks a graph neighbor's status 
{
 
	NodeLocationType  dif=m_CurrentNode->GetLocation()-m_NeighborNode->GetLocation();
//	std::cout << " visited? " << m_NeighborNode->GetVisited() << 
//		" old cost " << m_NeighborNode->GetTotalCost() << " new cost " <<m_NewCost << std::endl;
  if (!m_NeighborNode->GetVisited() && ! m_NeighborNode->GetUnVisitable() )
  {
	// set the cost and put into the queue
	m_NeighborNode->SetTotalCost(m_NewCost);
	float delt=fabs(m_CurrentNode->GetValue()-m_NeighborNode->GetValue());//*dif.magnitude();
	m_NeighborNode->SetValue(m_CurrentNode->GetValue()+delt);
	m_NeighborNode->SetPredecessor(m_CurrentNode);
	m_NeighborNode->SetVisited();



	  float mag = 0.0;
	  for (int jj=0; jj<GraphDimension;jj++) mag+=dif[jj]*dif[jj];
	  mag = sqrt(mag);
  m_NeighborNode->SetValue(m_CurrentNode->GetValue(2)+mag,2); // the actual manifold distance travelled
	m_QS->m_Q.push(m_NeighborNode);	
	
//	std::cout << " Pushing new node on " << m_NewCost << std::endl;
  }
  else if (m_NewCost < m_NeighborNode->GetTotalCost()&& ! m_NeighborNode->GetUnVisitable()  )
  {
//	  std::cout << " Updating " << std::endl;
	float delt=fabs(m_CurrentNode->GetValue()-m_NeighborNode->GetValue());//*dif.magnitude();
	m_NeighborNode->SetValue(m_CurrentNode->GetValue()+delt);
	m_NeighborNode->SetTotalCost(m_NewCost);
	m_NeighborNode->SetPredecessor(m_CurrentNode);

	  float mag = 0.0;
	  for (int jj=0; jj<GraphDimension;jj++) mag+=dif[jj]*dif[jj];
	  mag = sqrt(mag);
  m_NeighborNode->SetValue(m_CurrentNode->GetValue(2)+mag,2); // the actual manifold distance travelled
	m_QS->m_Q.push(m_NeighborNode);	
  }

}

template<class TGraphSearchNode > 
void ManifoldIntegrationAlgorithm<TGraphSearchNode>::FindPath()
{               
  if (m_QS->m_SourceNodes.empty())
  {
    std::cout << "ERROR !! DID NOT SET SOURCE!!\n";
	return;
  }
  
  std::cout << "MI start find path " << " Q size " << m_QS->m_Q.size() << " \n";
  
  while ( !m_SearchFinished && !m_QS->m_Q.empty()  )
  {
    m_CurrentNode=m_QS->m_Q.top();
    m_CurrentCost=m_CurrentNode->GetTotalCost();
    m_QS->m_Q.pop();
    if (!m_CurrentNode->GetDelivered())
	{
	  m_QS->IncrementTimer();
	  ///std::cout << " searching " << m_CurrentNode->GetLocation()   << " \n";
	  this->SearchEdgeSet();	
	  //if ( (m_CurrentNode->GetTimer() % 1.e5 ) == 0) 
		// std::cout << " searched  " << m_CurrentNode->GetTimer()   << " \n";
	}
    m_CurrentNode->SetDelivered();

  }  // end of while
       
	m_NumberSearched = (unsigned long) m_QS->GetTimer();
  std::cout << "Done with find path " << " Q size " << m_QS->m_Q.size() << 
    " num searched " << m_NumberSearched << " \n";

  std::cout << " Max Distance " << m_CurrentCost << std::endl;
  
  return;
              	
}




}
#endif
