#ifndef LVR2_MESH_CHUNK_ACTOR_HPP_
#define LVR2_MESH_CHUNK_ACTOR_HPP_

#include <vtkActor.h>

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkObjectFactory.h>
#include <vtkRenderingCoreModule.h>
#include <vtkProperty.h>
#include <vtkTexture.h>

class VTKRENDERINGCORE_EXPORT MeshChunkActor : public vtkActor
{
    public:
        vtkTypeMacro(MeshChunkActor, vtkActor);

        static MeshChunkActor* New();

        size_t getID() { return id_; }
        void setID(size_t id) { id_ = id; }
        bool getRender() { return render_; }
        void setRender(bool render) { render_ = render; }
    protected:
        size_t id_;
        bool render_;

    public:
        virtual void ReleaseGraphicsResources(vtkWindow *window);

        virtual int RenderOpaqueGeometry(vtkViewport *viewport);

        virtual int RenderTranslucentPolygonalGeometry(vtkViewport *viewport);

        virtual void Render(vtkRenderer *ren);

        void ShallowCopy(vtkProp *prop); 

    protected:
        vtkActor* Device;

        MeshChunkActor() {
            this->Device = vtkActor::New();
        }

        ~MeshChunkActor() {
            this->Device -> Delete();
        }
};

#endif
