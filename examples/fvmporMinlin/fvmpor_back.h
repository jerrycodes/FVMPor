#ifndef FVMPOR_H
#define FVMPOR_H

#include "definitions.h"
#include "shape.h"

#include <lin/impl/rebind.h>
#include <lin/lin.h>
#include <fvm/fvm.h>
#include <fvm/mesh.h>
#include <fvm/solver.h>
#include <fvm/physics_base.h>

#include <util/intvector.h>
#include <util/interpolation.h>
#include <util/dimvector.h>

#include <mkl_spblas.h>
#include <mkl_service.h>

#include <vector>
#include <memory>
#include <map>

namespace fvmpor {

template <typename T>
struct CoordTraits_{
    static bool is_device() {return false;};
};
template <>
struct CoordTraits_<lin::gpu::Coordinator<int> >{
    static bool is_device() {return true;};
};

enum SpatialWeightType {weightUpwind, weightAveraging, weightVanLeer};

using lin::all;

template <typename CoordHost, typename CoordDevice>
class VarSatPhysicsImpl{
public:
    typedef typename lin::rebind<CoordHost, double>::type CoordHostDouble;
    typedef typename lin::rebind<CoordHost, int>::type CoordHostInt;
    typedef typename lin::rebind<CoordDevice, double>::type CoordDeviceDouble;
    typedef typename lin::rebind<CoordDevice, int>::type CoordDeviceInt;

    typedef lin::Vector<double, CoordHostDouble> TVec;
    typedef lin::Vector<int, CoordHostInt> TIndexVec;
    typedef lin::Vector<double, CoordDeviceDouble> TVecDevice;
    typedef lin::Vector<int, CoordDeviceInt> TIndexVecDevice;

    typedef util::InterpolationMatrix<CoordDevice> InterpolationMatrix;
    typedef util::DimVector<TVecDevice> DimVector;
protected:
    typedef mesh::Point Point;

    // computation during a timestep
    void process_faces_lim( const mesh::Mesh &m );
    void process_faces_shape( const mesh::Mesh &m );
    void process_volumes_psk( const mesh::Mesh &m );
    void process_derivative_coefficients( const mesh::Mesh &m );
    void process_fluxes( double t, const mesh::Mesh &m );
    void process_spatial_weights(const mesh::Mesh& m);

    // physical zones
    const PhysicalZone& physical_zone( int ) const;
    int physical_zones() const;

    // boundary conditions
    int boundary_conditions() const { return boundary_conditions_h_.size(); };
    const BoundaryCondition& boundary_condition_h( int ) const;
    const Constants& constants() const { return constants_; };

    ////////////////////////////////
    // routines for setting up
    ///////////////////////////////
    void set_physical_zones();
    void set_boundary_conditions();
    void initialise_vectors( const mesh::Mesh &m );
    void set_initial_conditions( double &t, const mesh::Mesh& m );
    void set_constants();
    void initialise_shape_functions(const mesh::Mesh& m);

    // physics specific
    void saturation( TVecDevice& h, const PhysicalZone &props, TVecDevice &Sw, TVecDevice &dSw, TVecDevice &krw );

    // communicator for global communication of doubles on the nodes
    mpi::Communicator<double> node_comm_;

    // physical definitions
    int dimension;
    std::vector<PhysicalZone> physical_zones_;
    std::map<int,BoundaryCondition> boundary_conditions_h_;
    Constants constants_;
    // tags whether a node is dirichlet
    TIndexVec is_dirichlet_h_vec_;

    // spatial weighting
    int CV_flux_comm_tag;
    SpatialWeightType spatial_weighting;

    TIndexVecDevice CV_up; // DEVICE
    TVecDevice CV_flux; // DEVICE
    TIndexVecDevice edge_up; // DEVICE
    TIndexVecDevice edge_down; // DEVICE
    TVecDevice edge_flux; // DEVICE

    // derived quantities
    std::vector<TVecDevice> head_scv; // DEVICE
    std::vector<TVecDevice> phi_scv; // DEVICE
    std::vector<TVecDevice> dphi_scv; // DEVICE
    //std::vector<TVecDevice> Se_scv; // DEVICE
    std::vector<TVecDevice> Sw_scv; // DEVICE
    std::vector<TVecDevice> theta_scv; // DEVICE
    std::vector<TVecDevice> dSw_scv; // DEVICE
    std::vector<TVecDevice> krw_scv; // DEVICE
    std::vector<TIndexVecDevice> index_scv; // DEVICE
    std::vector<TVecDevice> weight_scv; // DEVICE
    std::map<int, int> zones_map_;

    // spatial weighting for CV faces
    std::vector<TIndexVecDevice> n_front_; // DEVICE
    std::vector<TIndexVecDevice> n_back_; // DEVICE
    std::vector<TIndexVecDevice> p_front_; // DEVICE
    std::vector<TIndexVecDevice> q_front_; // DEVICE
    std::vector<TIndexVecDevice> p_back_; // DEVICE
    std::vector<TIndexVecDevice> q_back_; // DEVICE
    TVecDevice edge_weight_front_; // DEVICE
    TVecDevice edge_weight_back_; // DEVICE
    TIndexVecDevice edge_node_front_; // DEVICE
    TIndexVecDevice edge_node_back_; // DEVICE

    // stores list of nodes on seepage faces
    //TIndexVec seepage_nodes;
    //int seepage_tag; // unique tag for the seepage BC

    // for interpolation from nodes to CV faces
    InterpolationMatrix shape_matrix;
    InterpolationMatrix shape_gradient_matrixX;
    InterpolationMatrix shape_gradient_matrixY;
    InterpolationMatrix shape_gradient_matrixZ;
    InterpolationMatrix flux_lim_matrix;

    TVecDevice h_vec; // head at the nodes // DEVICE
    TVecDevice M_vec; // M at the nodes // DEVICE
    DimVector grad_h_faces_; // head gradient at CV faces // DEVICE
    TVecDevice h_faces; // head at CV faces // DEVICE
    TVecDevice M_flux_faces; // mass flux at CV faces // DEVICE HOST - need both for easy access in the FVM_Assembler
    TVecDevice qdotn_faces; // volumetric fluid flux at CV faces // DEVICE

    // storing derived quantities averaged for each control volume
    TVecDevice rho_vec, Sw_vec, dSw_vec, theta_vec; // DEVICE
    TVecDevice phi_vec, dphi_vec; // DEVICE
    // storing derived quantities at cv faces (using c and h values at faces)
    TVecDevice rho_faces; // DEVICE
    // storing upwinded/flux limitted values at cv faces
    TVec rho_faces_lim, krw_faces_lim; // DEVICE
    // storing coefficients for derivative terms
    TVecDevice ahh_vec; // DEVICE
    // storing values at faces
    DimVector K_faces_; // DEVICE
    DimVector norm_faces_; // DEVICE
    DimVector qsat_faces_; // DEVICE HOST
};

template <typename value_type, typename CoordHost, typename CoordDevice>
class VarSatPhysics : public fvm::PhysicsBase<VarSatPhysics<value_type, CoordHost, CoordDevice>, value_type>, public VarSatPhysicsImpl<CoordHost,CoordDevice> {
    typedef fvm::PhysicsBase<VarSatPhysics, value_type> base;
    //typedef VarSatPhysicsImpl<CoordHost,CoordDevice> impl;
    int num_calls;
    friend class Preconditioner;
public:

    typedef typename base::iterator iterator;
    typedef typename base::const_iterator const_iterator;
    typedef typename base::Callback Callback;

    VarSatPhysics() : num_calls(0) {};
    int calls() const { return num_calls; }

    /////////////////////////////////
    // GLOBAL
    /////////////////////////////////
    value_type flux(double t, const mesh::CVFace& cvf, const_iterator sol) const;
    value_type boundary_flux(double t, const mesh::CVFace& cvf, const_iterator sol) const;

    double compute_mass(const mesh::Mesh& m, const_iterator u);
    double mass_flux_per_time(const mesh::Mesh& m);

    /////////////////////////////////
    // VARIABLE-SPECIFIC
    /////////////////////////////////
    void initialise(double& t, const mesh::Mesh& m, iterator u, iterator udash, iterator temp, Callback);
    void preprocess_evaluation(double t, const mesh::Mesh& m, const_iterator u, const_iterator udash);
    void preprocess_timestep(double t, const mesh::Mesh& m, const_iterator sol, const_iterator deriv);
    value_type lhs(double t, const mesh::Volume& volume, const_iterator u, const_iterator udash) const;
    value_type dirichlet(double t, const mesh::Node& n) const;
};

// **************************************************************************
// *                          IMPLEMENTATION                                *
// **************************************************************************
    using mesh::Point;

    template <typename TVec>
    void density(TVec& h, TVec& rho, const Constants& constants)
    {
        double beta = constants.beta();
        double rho_0 = constants.rho_0();
        double g = constants.g();

        if( beta ){
            rho = h;
            rho *= rho_0*rho_0*g*beta;
            rho += rho_0;
        }else{
            rho(all) = rho_0;
        }
    }

    template <typename TVec>
    void porosity(TVec& h, TVec& phi, TVec& dphi, const PhysicalZone& props, const Constants& constants)
    {
        double g = constants.g();
        double rho_0 = constants.rho_0();
        double phi_0 = props.phi;
        double alpha = props.alpha;

        // porosity
        if(alpha==0.){
            phi(all) = phi_0;
            dphi(all) = 0.;
        }
        else{
            double factor = (phi_0-1.)*rho_0*g*alpha;
            phi(all) = 1.;
            phi(all) += factor * h;
        }
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::saturation( TVecDevice& h, const PhysicalZone &props, TVecDevice &Sw, TVecDevice &dSw, TVecDevice &krw )
    {
        double alphaVG = props.alphaVG;
        double nVG = props.nVG;
        double mVG = props.mVG;
        double S_r = props.S_r;
        double phi = props.phi;

        if( CoordTraits_<CoordDeviceInt>::is_device() ){
            const double *h_ptr = h.data();
            double *dSw_ptr = dSw.data();
            double *Sw_ptr  = Sw.data();
            double *krw_ptr = krw.data();
            lin::gpu::saturation(h_ptr, Sw_ptr, dSw_ptr, krw_ptr, h.dim(), alphaVG, nVG, mVG, S_r, phi);
        }
        else{
            // if a = (alpha*|h|)^n, and b = 1+a
            // set dSw = a
            dSw(all) = -alphaVG*h;
            dSw.pow(nVG);

            // Sw = 1/b
            Sw(all) = dSw+1.;
            krw(all) = -1.;
            krw /= Sw;

            // dSw /= b
            dSw /= Sw;

            // Sw = 1/(b^m)
            // this is the final value for Sw
            Sw.pow(-mVG);

            // find dSw
            dSw *= Sw;
            dSw /= h;
            dSw *= -(1-S_r)*(nVG-1);

            // find krw
            krw += 1.;
            krw.pow(mVG);
            krw -= 1.;
            krw.pow(2);
            krw(all) *= sqrt(Sw);
            Sw *= (1-S_r);
            Sw += S_r;

            // now override values for saturated h
            int n=h.dim();
            for(int i=0; i<n; i++){
                if(h.at(i)>=0.){
                    dSw.at(i) = 0.;
                    Sw.at(i) = 1.;
                    krw.at(i) = 1.;
                }
            }
        }
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::initialise_vectors( const mesh::Mesh &m ){
        dimension = m.dim();

	std::cout << "mesh has " << m.nodes() << " nodes " << m.elements() << " elements and " << m.cvfaces() << " CV faces" << std::endl;
        node_comm_.set_pattern( "NP_double", m.node_pattern() );

        // set physical properties
        set_constants();
        set_physical_zones();
        set_boundary_conditions();

        // initialise space for storing p-s-k values
        int N = m.nodes();
        Sw_vec = TVecDevice(N);
        dSw_vec = TVecDevice(N);
        rho_vec = TVecDevice(N);
        theta_vec = TVecDevice(N);
        phi_vec = TVecDevice(N);
        dphi_vec = TVecDevice(N);

        rho_faces_lim = TVecDevice(m.interior_cvfaces());
        krw_faces_lim = TVecDevice(m.interior_cvfaces());
        rho_faces = TVecDevice(m.interior_cvfaces());

        // spatial weightings
        CV_up = TIndexVec(m.local_nodes());
        CV_flux = TVec(m.nodes());
        CV_flux_comm_tag = node_comm_.vec_add(CV_flux.data());

        edge_up = TIndexVecDevice(m.edges());
        edge_down = TIndexVecDevice(m.edges());
        edge_flux = TVecDevice(m.edges());

        M_flux_faces = TVecDevice(m.cvfaces());
        qdotn_faces = TVecDevice(m.cvfaces());

        // initialise space for derivative coefficients
        int NL = m.local_nodes();
        ahh_vec = TVecDevice( NL );

        // tag dirichlet nodes
        // also tag nodes that lie on seepage faces
        // assumes that if there is more than one seepage face, they all have the same tag
        TIndexVecDevice is_dirichlet_h_vec( m.local_nodes() );
        for( int i=0; i<m.local_nodes(); i++ ){
            const mesh::Node& n = m.node(i);
            // look for dirichlet tags attached to the node
            for( int j=0; j<n.boundaries(); j++ ){
                int tag = n.boundary(j);
                if( boundary_condition_h(tag).is_dirichlet() ){
                    is_dirichlet_h_vec[i] = tag;
                }
            }
        }
        // copy to device
        is_dirichlet_h_vec_ = is_dirichlet_h_vec;

        // initialise vectors used in calculating derived quantities such as saturation
        // allocate room for each of the arrays
        std::set<int> zones;
        for(int i=0; i<m.elements(); i++)
            zones.insert(m.element(i).physical_tag());
        int num_zones = zones.size();
        int indx=0;
        for( std::set<int>::iterator it=zones.begin(); it!=zones.end(); it++)
            zones_map_[*it] = indx++;

        // temp var
        std::vector< std::vector<double> > weight_scv_tmp;
        std::vector< std::vector<int> > index_scv_tmp;
        weight_scv_tmp.resize( num_zones );
        index_scv_tmp.resize( num_zones );
        std::vector<std::map<int,int> > nodes_idx;
        nodes_idx.resize(num_zones);
        // compile index and weight information mapping node information to scv information
        for(int i=0; i<m.nodes(); i++){
            const mesh::Volume& cv = m.volume(i);
            double cv_vol = cv.vol();

            std::vector<double> weights(num_zones);
            std::vector<int> counts(num_zones);
            for(int j=0; j<cv.scvs(); j++){
                int tag = zones_map_[cv.scv(j).element().physical_tag()];
                assert(tag<num_zones);
                weights[tag] += cv.scv(j).vol() / cv_vol;
                counts[tag]++;
            }
            for(int j=0; j<num_zones; j++){
                if(counts[j]){
                    weight_scv_tmp[j].push_back(weights[j]);
                    index_scv_tmp[j].push_back(i);
                    nodes_idx[j][i] = index_scv_tmp[j].size()-1;
                }
            }
        }
        weight_scv.resize( num_zones );
        index_scv.resize( num_zones );
        for(int i=0; i<num_zones; i++){
            // DEBUG : does this copy the data efficiently, using the iterator interface?
            // NO! this needs to be fixed otherwise setup for large problems will be a problem
            //weight_scv[i].assign(weight_scv_tmp[i].begin(), weight_scv_tmp[i].end());
            //index_scv[i].assign(index_scv_tmp[i].begin(), index_scv_tmp[i].end());
            TVec w_tmp(weight_scv_tmp[i].begin(), weight_scv_tmp[i].end());
            TIndexVec i_tmp(index_scv_tmp[i].begin(), index_scv_tmp[i].end());
            weight_scv[i] = w_tmp;
            index_scv[i] = i_tmp;
        }

        // allocate room for head values mapped onto SCVs
        head_scv.resize( num_zones );
        phi_scv.resize( num_zones );
        dphi_scv.resize( num_zones );
        //Se_scv.resize( num_zones );
        Sw_scv.resize( num_zones );
        theta_scv.resize( num_zones );
        dSw_scv.resize( num_zones );
        krw_scv.resize( num_zones );
        for(int i=0; i<num_zones; i++){
            head_scv[i] = TVecDevice( index_scv[i].size() );
            phi_scv[i] = TVecDevice( index_scv[i].size() );
            dphi_scv[i] = TVecDevice( index_scv[i].size() );
            //Se_scv[i] = TVecDevice( index_scv[i].size() );
            Sw_scv[i] = TVecDevice( index_scv[i].size() );
            theta_scv[i] = TVecDevice( index_scv[i].size() );
            dSw_scv[i] = TVecDevice( index_scv[i].size() );
            krw_scv[i] = TVecDevice( index_scv[i].size() );
        }

        // this will hold global (face, edge) pairs of each mapped node value in each zone
        std::vector<std::multimap<int, std::pair<int, int> > > faceEdge_map_front;
        std::vector<std::multimap<int, std::pair<int, int> > > faceEdge_map_back;
        faceEdge_map_front.resize(num_zones);
        faceEdge_map_back.resize(num_zones);
        for( int i=0; i<m.edges(); i++ ){
            const std::vector<int>& edge_cvfaces = m.edge_cvface(i);
            int fid = m.edge(i).front().id();
            int bid = m.edge(i).back().id();
            for(int j=0; j<edge_cvfaces.size(); j++){
                int f = edge_cvfaces[j];
                int z = zones_map_[m.cvface(f).element().physical_tag()];
                int n = nodes_idx[z][fid];
                faceEdge_map_front[z].insert(std::pair<int, std::pair<int, int> >( n, std::pair<int, int>(f, i) ));
                n = nodes_idx[z][bid];
                faceEdge_map_back[z].insert(std::pair<int, std::pair<int, int> >( n, std::pair<int, int>(f, i)));
            }
        }

        // should also reserve memory for the vectors
        n_front_.resize(num_zones);
        p_front_.resize(num_zones);
        q_front_.resize(num_zones);
        n_back_.resize(num_zones);
        p_back_.resize(num_zones);
        q_back_.resize(num_zones);
        typedef std::multimap<int, std::pair<int, int> >::iterator idxTypeIt;
        for(int z=0; z<num_zones; z++){
            std::vector<int> n_front;
            std::vector<int> p_front;
            std::vector<int> q_front;
            std::vector<int> n_back;
            std::vector<int> p_back;
            std::vector<int> q_back;
            int len = head_scv[z].dim();
            for(int i=0; i<len; i++){
                std::pair<idxTypeIt, idxTypeIt> rng = faceEdge_map_front[z].equal_range(i);
                for( idxTypeIt it=rng.first; it!=rng.second; ++it ){
                    n_front.push_back(i); // local node id
                    q_front.push_back(it->second.first); // global face index
                    p_front.push_back(it->second.second); // global edge index
                }
                rng = faceEdge_map_back[z].equal_range(i);
                for( idxTypeIt it=rng.first; it!=rng.second; ++it ){
                    n_back.push_back(i); // local node id
                    q_back.push_back(it->second.first); // global face index
                    p_back.push_back(it->second.second); // global edge index
                }
            }
            n_front_[z] = TIndexVec(n_front.begin(), n_front.end());
            p_front_[z] = TIndexVec(p_front.begin(), p_front.end());
            q_front_[z] = TIndexVec(q_front.begin(), q_front.end());
            n_back_[z] = TIndexVec(n_back.begin(), n_back.end());
            p_back_[z] = TIndexVec(p_back.begin(), p_back.end());
            q_back_[z] = TIndexVec(q_back.begin(), q_back.end());
        }

        edge_weight_front_ = TVecDevice(m.edges(), 0.5);
        edge_weight_back_ = TVecDevice(m.edges()), 0.5;
        TIndexVec edge_node_front(m.edges());
        TIndexVec edge_node_back(m.edges());
        for( int i=0; i<m.edges(); i++){
            edge_node_front[i] = m.edge(i).front().id();
            edge_node_back[i] = m.edge(i).back().id();
        }
        // copy onto device
        edge_node_front_ = edge_node_front;
        edge_node_back_ = edge_node_back;

        // initialise the shape functions
        initialise_shape_functions(m);

        // initialise flux vecs
        qsat_faces_.set(m.interior_cvfaces(), m.dim());

        norm_faces_.set(m.interior_cvfaces(), m.dim());
        TVec X(m.interior_cvfaces());
        TVec Y(m.interior_cvfaces());
        TVec Z(m.interior_cvfaces());
        for( int i=0; i<m.interior_cvfaces(); i++ ){
            Point nrm = m.cvface(i).normal();
            X[i] = nrm.x;
            Y[i] = nrm.y;
            if( m.dim()==3 )
                Z[i] = nrm.z;
        }
        norm_faces_.x() = X;
        norm_faces_.y() = Y;
        if(m.dim()==3)
            norm_faces_.z() = Z;

        K_faces_.set(m.interior_cvfaces(), m.dim());
        for( int i=0; i<m.interior_cvfaces(); i++ ){
            int tag = m.cvface(i).element().physical_tag();
            X[i] = -physical_zone(tag).K_xx;
            Y[i] = -physical_zone(tag).K_yy;
            if( m.dim()==3 )
                Z[i] = -physical_zone(tag).K_zz;
        }
        K_faces_.x() = X;
        K_faces_.y() = Y;
        K_faces_.z() = Z;
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_faces_shape( const mesh::Mesh &m )
    {
        density(h_faces, rho_faces, constants());
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_faces_lim( const mesh::Mesh &m )
    {
        for( int e=0; e<m.edges(); e++ ){
            double rho_edge = rho_vec.at(edge_node_back_[e])*edge_weight_back_.at(e) + rho_vec.at(edge_node_front_[e])*edge_weight_front_.at(e);

            const std::vector<int>& edge_cvfaces = m.edge_cvface(e);
            for(int j=0; j<edge_cvfaces.size(); j++){
                int face = edge_cvfaces[j];
                rho_faces_lim.at( face ) = rho_edge;
            }
        }
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_fluxes( double t, const mesh::Mesh &m )
    {
        // compute the vector quantity q at each internal CV face
        qsat_faces_.x().at(all) = grad_h_faces_.x().at(all);
        qsat_faces_.x() *= K_faces_.x();
        qsat_faces_.y().at(all) = grad_h_faces_.y().at(all);
        if( m.dim()==2 ){
            qsat_faces_.y() += 1.;
        }else{
            qsat_faces_.z().at(all) = grad_h_faces_.z().at(all);
            qsat_faces_.z() += 1.;
            qsat_faces_.z() *= K_faces_.z();
        }
        qsat_faces_.y() *= K_faces_.y();
        
        //qsat_faces_.dot(norm_faces_, qdotn_faces(1,m.interior_cvfaces()));
        qdotn_faces(1,m.interior_cvfaces()) = mul(norm_faces_.x()(all), qsat_faces_.x()(all));
        qdotn_faces(1,m.interior_cvfaces()) += mul(norm_faces_.y()(all), qsat_faces_.y()(all));
        if( m.dim()==3 ){
            qdotn_faces(1,m.interior_cvfaces()) += mul(norm_faces_.z()(all), qsat_faces_.z()(all));
        }

        // find the velocity at each CV face
        // these can be written as one operation when subranges are allowed
        for( int i=0; i<m.interior_cvfaces(); i++ ){
            qdotn_faces.at(i) *= krw_faces_lim.at(i);
            M_flux_faces.at(i) = rho_faces_lim.at(i) * qdotn_faces.at(i);
        }

        // loop over boundary faces and find fluid flux where explicitly given by BCs
        for( int i=m.interior_cvfaces(); i<m.cvfaces(); i++)
        {
            const mesh::CVFace& cvf = m.cvface(i);

            int boundary_tag = cvf.boundary();
            const BoundaryCondition& BCh = boundary_condition_h( boundary_tag );

            switch( BCh.type() ){
                // prescribed flux
                case 3:
                    qdotn_faces.at(i) = BCh.value(t) * cvf.area();
                    break;
                // prescribed directional flux
                case 6:
                    qdotn_faces.at(i) = BCh.flux( t, cvf.normal() ) * cvf.area();
                    break;
                // seepage
                case 7:
                    qdotn_faces.at(i) = BCh.value(t) * cvf.area();
                    break;
                // seepage/hydrostatic shoreline
                case 8:
                    qdotn_faces.at(i) = 0. * cvf.area();
                    break;
                default:
                    break;

            }
        }

        /*
        for( int i=m.interior_cvfaces(); i<m.cvfaces(); i++)
        {
            const mesh::CVFace& cvf = m.cvface(i);

            int boundary_tag = cvf.boundary();
            const BoundaryCondition& BCh = boundary_condition_h( boundary_tag );

            //if( BCh.is_dirichlet() || BCh.type()==7 )
            if( is_dirichlet_h_vec_[cvf.back().id()] )
            {
                // in the case of a dirichlet boundary condition on pressure head we calculate the
                // flux over the control volume face by asserting conservation of mass
                double total_flux = 0.0;
                double total_area = 0.0;

                const mesh::Volume& v = cvf.back().volume();
                for (int ii = 0; ii < v.scvs(); ++ii) {
                    const mesh::SCV& scv = v.scv(ii);
                    for (int j = 0; j < scv.cvfaces(); ++j) {
                        const mesh::CVFace& subcvf = scv.cvface(j);
                        if( subcvf.id()<m.interior_cvfaces() || (!boundary_condition_h(subcvf.boundary()).is_dirichlet() && boundary_condition_h(subcvf.boundary()).type()!=7 && boundary_condition_h(subcvf.boundary()).type()!=8) ){
                            int sign = subcvf.back().id() == v.id() ? 1 : -1;
                            total_flux += sign * qdotn_faces.at(subcvf.id());
                        } else{
                            total_area += subcvf.area();
                        }
                    }
                }
                qdotn_faces.at(i) = -total_flux / total_area * cvf.area();
            }
        }
        */

        // find mass flux over each boundary
        for( int i=m.interior_cvfaces(); i<m.cvfaces(); i++)
        {
            const mesh::CVFace& cvf = m.cvface(i);

            // choose an appropriate concentration and density if the flow is into the domain
            double rho_face = rho_vec[cvf.back().id()];
            if( qdotn_faces.at(i)>=0 ){
               rho_face = constants().rho_0();
            }

            // now form the flux over the face
            M_flux_faces.at(i) = rho_face * qdotn_faces.at(i);
        }

        /*
        for( int i=0; i<m.cvfaces(); i++ )
            std::cout << M_flux_faces[i] << " ";
        std::cout << std::endl;
        */
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_spatial_weights(const mesh::Mesh& m){

        // determine the flux over each edge
        flux_lim_matrix.matvec( qdotn_faces, edge_flux );

        switch( spatial_weighting ){
            ////////////////////////////////////////////////////////
            // the upwinding case is simple
            ////////////////////////////////////////////////////////
            case weightUpwind : 
                for(int i=0; i<m.edges(); i++){
                    if( edge_flux.at(i)<0. ){
                        edge_weight_back_.at(i) = 0.;
                        edge_weight_front_.at(i) = 1.;
                        edge_up[i] = m.edge(i).front().id();
                        edge_down[i] = m.edge(i).back().id();
                    }
                    else{
                        edge_weight_back_.at(i) = 1.;
                        edge_weight_front_.at(i) = 0.;
                        edge_up[i] = m.edge(i).back().id();
                        edge_down[i] = m.edge(i).front().id();
                    }
                }
                break;
            ////////////////////////////////////////////////////////
            // the flux limitting case takes a bit more work
            ////////////////////////////////////////////////////////
            case weightVanLeer :
                for(int i=0; i<m.edges(); i++){
                    if( edge_flux.at(i)>0. ){
                        edge_up[i] = m.edge(i).back().id();
                        edge_down[i] = m.edge(i).front().id();
                    }
                    else{
                        edge_up[i] = m.edge(i).front().id();
                        edge_down[i] = m.edge(i).back().id();
                    }
                }

                // find the up node for each CV
                for(int i=0; i<m.local_nodes(); i++){
                    CV_flux.at(i) = 0.;
                    CV_up[i] = -1;
                }
                // set the flux into each boundary node to be that from over the boundary
                for(int i=m.interior_cvfaces(); i<m.cvfaces(); i++){
                    int n=m.cvface(i).back().id();
                    CV_flux.at(n) -= qdotn_faces.at(i);
                }

                // now find max flux into each CV
                for(int i=0; i<m.edges(); i++){
                    if( edge_node_front_[i]<m.local_nodes() || edge_node_back_[i]<m.local_nodes() ){
                        int CV = edge_down[i];
                        if( CV<m.local_nodes() ){
                            double fl = fabs(edge_flux.at(i));
                            if( fl>CV_flux[CV] ){
                                CV_flux[CV] = fl;
                                CV_up[CV] = edge_up[i];
                            }
                        }
                    }
                }

                // verify that each CV was assigned an upwind point
                for(int i=0; i<m.local_nodes(); i++){
                    if(CV_up[i]==-1){
                        CV_up[i] = i;
                    }
                }

                *node_comm_.mpicomm() << "VarSatPhysicsImpl::process_spatial_weights : communicating 2up fluxes values accross subdomain boundaries" << std::endl;
                node_comm_.send(CV_flux_comm_tag);
                node_comm_.recv(CV_flux_comm_tag);

                // find r and sigma for each edge
                for(int i=0; i<m.edges(); i++){
                    if( edge_node_front_[i]<m.local_nodes() || edge_node_back_[i]<m.local_nodes() ){
                        double qup = fabs(edge_flux.at(i));
                        double q2up = CV_flux.at(edge_up[i]);
                        double r = q2up / qup;
                        double sigma;
                        if( qup==0. )
                            sigma = 1.;
                        else if(r>1.e10)
                            sigma = 2.;
                        else
                            sigma = (r+fabs(r)) / (1.+fabs(r));

                        if( edge_flux.at(i)>0. ){
                            edge_weight_back_.at(i) = sigma/2.;
                            edge_weight_front_.at(i) = 1.-sigma/2.;
                        }
                        else{
                            edge_weight_back_.at(i) = 1.-sigma/2.;
                            edge_weight_front_.at(i) = sigma/2.;
                        }
                    }
                }
        }
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_volumes_psk( const mesh::Mesh &m )
    {
        double beta = constants().beta();
        double rho_0 = constants().rho_0();
        double g = constants().g();

        // zero out vectors of CV-averaged derived quantities
        phi_vec(all) = 0.;
        dphi_vec(all) = 0.;
        Sw_vec(all) = 0.;
        dSw_vec(all) = 0.;
        theta_vec(all) = 0.;
        krw_faces_lim(all) = 0.;

        // for each zone calucluate the scv-weighted derived quantities and add them to the appropriated CV-averaged vectors
        double T=0.;
        for( std::map<int, int>::iterator it=zones_map_.begin(); it!=zones_map_.end(); it++){
            int zone = (*it).second;
            int indx = (*it).first;
            int n = index_scv.size();
            const PhysicalZone& props = physical_zone(indx);

            // get head data for this zone type
            std::cerr << "1" << std::endl;
            double t1 = -MPI_Wtime();
            head_scv[zone].at(all) = h_vec.at(index_scv[zone]);
            t1+=MPI_Wtime();
            std::cerr << "1.." << std::endl;

            // find porosity and scale by weights
            std::cerr << "2" << std::endl;
            double t2 = -MPI_Wtime();
            porosity(head_scv[zone], phi_scv[zone], dphi_scv[zone], props, constants());
            t2+=MPI_Wtime();
            std::cerr << "2.." << std::endl;

            // the order of scalings below is deliberate so as to reduce flop counts
            // effective saturation
            std::cerr << "3" << std::endl;
            double t3 = -MPI_Wtime();
            saturation( head_scv[zone], props, Sw_scv[zone], dSw_scv[zone], krw_scv[zone] );
            t3 += MPI_Wtime();
            std::cerr << "3.." << std::endl;

            // saturation
            std::cerr << "4" << std::endl;
            double t4 = -MPI_Wtime();
            // moisture content
            theta_scv[zone](all) = mul(Sw_scv[zone], phi_scv[zone]);
            //theta_scv[zone] *= Sw_scv[zone];
            t4 += MPI_Wtime();
            std::cerr << "4.." << std::endl;

            // copy into global vector
            double t5 = -MPI_Wtime();
            //copy_indirect(int N, const double *rhs, int incx, double *lhs, int incy, const int* p, int incp, PermFlagT side_flag){
            int N = index_scv[zone].dim();
            copy_indirect(N, const double *rhs, int incx, double *lhs, int incy, const int* p, int incp, PermFlagT side_flag){
            //phi_vec.at(index_scv[zone]) +=  mul(phi_scv[zone], weight_scv[zone]);
            dphi_vec.at(index_scv[zone]) += mul(dphi_scv[zone], weight_scv[zone]);
            Sw_vec.at(index_scv[zone]) += mul(Sw_scv[zone], weight_scv[zone]);
            dSw_vec.at(index_scv[zone]) += mul(dSw_scv[zone], weight_scv[zone]);
            theta_vec.at(index_scv[zone]) += mul(theta_scv[zone], weight_scv[zone]);
            t5 += MPI_Wtime();

            double t6 = -MPI_Wtime();
            krw_faces_lim.at(q_front_[zone]) = mul(krw_scv[zone].at(n_front_[zone]), edge_weight_front_.at(p_front_[zone]));
            krw_faces_lim.at(q_back_[zone]) += mul(krw_scv[zone].at(n_back_[zone]), edge_weight_back_.at(p_back_[zone]));
            t6 += MPI_Wtime();

            std::cout << t1 << ", " <<  t2 << ", " <<  t3 << ", " <<  t4 << ", " <<  t5 << ", " <<  t6 << std::endl;
            exit(0);
        }

        // find the CV-averaged density - this is much simpler because density is not dependant on material properties
        // of the porous medium
        density(h_vec, rho_vec, constants());
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::process_derivative_coefficients( const mesh::Mesh &m )
    {
        double rho_0 = constants().rho_0();
        double g = constants().g();
        double beta = constants().beta();

        double factor = rho_0*rho_0*g*beta;
        for( int i=0; i<ahh_vec.dim(); i++ )
            ahh_vec.at(i) = rho_vec.at(i)*phi_vec.at(i)*dSw_vec.at(i) + rho_vec.at(i)*Sw_vec.at(i)*dphi_vec.at(i) + factor*phi_vec.at(i)*Sw_vec.at(i);
    }

    template <typename CoordHost, typename CoordDevice>
    void VarSatPhysicsImpl<CoordHost,CoordDevice>::initialise_shape_functions(const mesh::Mesh& m)
    {
        // matrices with weights for computing shape functions
        TIndexVec ia, ja;
        TVec shape_val, shape_dx, shape_dy, shape_dz;

        // Allocate row begin array
        int ia_length = m.interior_cvfaces() + 1;
        ia = TIndexVec(ia_length);

        // Fill row begin array
        ia[0] = 0;
        for (int i = 0; i < m.interior_cvfaces(); ++i) {
            ia[i+1] = ia[i] + m.cvface(i).element().nodes();
        }

        // Allocate matrix arrays
        int ja_length = ia[ia_length-1];
        ja = TIndexVec(ja_length);

        shape_val = TVec(ja_length);
        shape_dx = TVec(ja_length);
        shape_dy = TVec(ja_length);
        shape_dz = TVec(ja_length);

        // Allocate node value arrays
        h_vec = TVec(m.nodes());

        // Allocate CVFace centroid arrays
        h_faces = TVec(m.interior_cvfaces());
        grad_h_faces_.set(m.interior_cvfaces(), m.dim());

        // Fill other arrays;
        for (int i = 0; i < m.elements(); ++i) {

            const mesh::Element& e = m.element(i);

            // Sort the node ids, to get the index vector
            std::vector< std::pair<int, int> > index_vector(e.nodes());
            for (int k = 0; k < e.nodes(); ++k) {
                index_vector[k] = std::make_pair(e.node(k).id(), k);
            }
            std::sort(index_vector.begin(), index_vector.end());

            shape::Shape my_shape(e);
            for (int j = 0; j < e.edges(); ++j) {

                int cvf_id = e.cvface(j).id();

                // Record ja indices
                const mesh::CVFace& cvf = e.cvface(j);
                for (int k = 0, p = ia[cvf_id]; p < ia[cvf_id+1]; ++k, ++p) {
                    ja[p] = index_vector[k].first;
                }

                // Get shape functions and gradients
                std::vector<double> shape_functions = my_shape.shape_functions(j);
                std::vector<mesh::Point> shape_gradients = my_shape.shape_gradients(j);

                // Now load them into the matrices
                for (int k = 0, p = ia[cvf_id]; p < ia[cvf_id+1]; ++k, ++p) {
                    shape_val[p] = shape_functions[index_vector[k].second];
                    shape_dx[p]  = shape_gradients[index_vector[k].second].x;
                    shape_dy[p]  = shape_gradients[index_vector[k].second].y;
                    shape_dz[p]  = shape_gradients[index_vector[k].second].z;
                }
            }
        }

        shape_matrix = InterpolationMatrix(ia, ja, shape_val);
        shape_gradient_matrixX = InterpolationMatrix(ia, ja, shape_dx);
        shape_gradient_matrixY = InterpolationMatrix(ia, ja, shape_dy);
        if (dimension == 3)
            shape_gradient_matrixZ = InterpolationMatrix(ia, ja, shape_dz);

        // matrix for flux limitting
        TIndexVec ia_fl, ja_fl;
        TVec weights_fl;

        // allocate space for row begin indices
        ia_length = m.edges()+1;
        ia_fl = TIndexVec(ia_length);
        ia_fl[0] = 0;
        for (int i = 0; i < m.edges(); ++i) {
            ia_fl[i+1] = ia_fl[i] + m.edge_cvface(i).size();
        }

        // allocate space for column indices
        ja_length = ia_fl[ia_length-1];
        ja_fl = TIndexVec(ja_length);

        // allocate space for weights
        //weights_fl.resize(ja_length);
        weights_fl = TVec(ja_length, 0.);

        for(int i=0; i<m.edges(); i++){
            const std::vector<int>& faces = m.edge_cvface(i);

            // determine the total surface area of the faces attached to edge i
            double total_area = 0.;
            for(int j=0; j<faces.size(); j++)
                total_area += m.cvface(faces[j]).area();

            // now determine the scaled weights
            int pos = ia_fl[i];
            for(int j=0; j<faces.size(); j++){
                int face = faces[j];
                //weights_fl[pos] = m.cvface(face).area()/total_area;
                weights_fl.at(pos) = 1./total_area;
                ja_fl[pos] = face;
                pos++;
            }
        }

        flux_lim_matrix = InterpolationMatrix(ia_fl, ja_fl, weights_fl);
    }

    // get a copy of a set of physical zone properties
    template <typename CoordHost, typename CoordDevice>
    const PhysicalZone& VarSatPhysicsImpl<CoordHost,CoordDevice>::physical_zone( int zone ) const
    {
        if(!(zone>=0 && zone<physical_zones_.size()))
        assert(zone>=0 && zone<physical_zones_.size());
        return physical_zones_[zone];
    }

    // get the number of physical zones
    template <typename CoordHost, typename CoordDevice>
    int VarSatPhysicsImpl<CoordHost,CoordDevice>::physical_zones( void ) const
    {
        return physical_zones_.size();
    }

    template <typename CoordHost, typename CoordDevice>
    const BoundaryCondition& VarSatPhysicsImpl<CoordHost,CoordDevice>::boundary_condition_h( int tag ) const{
        std::map<int,BoundaryCondition>::const_iterator it = boundary_conditions_h_.find(tag);
        assert( it!=boundary_conditions_h_.end());
        return it->second;
    }
} // end namespace fvmpor

#endif
