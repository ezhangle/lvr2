#ifndef OptimizeStage_HPP__
#define OptimizeStage_HPP__

#include "AbstractStage.hpp"
#include "BlockingQueue.hpp"
#include <boost/any.hpp>
#include <reconstruction/FastReconstruction.hpp>
#include <reconstruction/TSDFGrid.hpp>
#include <reconstruction/PointsetSurface.hpp>
#include <reconstruction/FastBox.hpp>
#include <io/PointBuffer.hpp>
#include <io/DataStruct.hpp>
#include <geometry/HalfEdgeVertex.hpp>
#include <geometry/HalfEdgeMesh.hpp>
#include <geometry/BoundingBox.hpp>
#include <kfusion/types.hpp>


using namespace lvr;
using namespace std;
using namespace kfusion;

typedef Vertex<float>  fVertex;
typedef ColorVertex<float, unsigned char> cVertex;
typedef FastBox<ColorVertex<float, unsigned char>, lvr::Normal<float> > cFastBox;
typedef TsdfGrid<cVertex, cFastBox, kfusion::Point> TGrid;
typedef FastReconstruction<ColorVertex<float, unsigned char>, lvr::Normal<float>, cFastBox > cFastReconstruction;
typedef HalfEdgeMesh<cVertex, lvr::Normal<float> > HMesh;
typedef HMesh* MeshPtr;

class OptimizeStage : public AbstractStage
{
public:

	// default constructor
	OptimizeStage(Options* options);

	virtual void firstStep();
	virtual void step();
	virtual void lastStep();
	
private:
	
	size_t mesh_count_;
	MeshPtr optiMesh_;
	Options* options_;
	
	MeshBufferPtr meshBufferPtr;
	size_t bounding_counter,pic_count_,texture_counter;
	
};
#endif // STAGE
