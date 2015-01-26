#include <cmath>
#include <iostream>

#include "Trial.h"
#include "Point.h"
#include "particledefine.h"
#include "GlobalData.h"

Trial::Trial(const GlobalData *_gdata) : Problem(_gdata)
{
	// Size and origin of the simulation domain
	lx = 1.0;
	ly = 1.0;
	lz = 3.0;
	H = 0.6;
	wet = false;

	m_usePlanes = true;

	m_size = make_double3(lx, ly, lz);
	m_origin = make_double3(0.0, 0.0, 0.0);

	// SPH parameters
	set_deltap(0.010f);
	m_simparams.dt = 0.0001f;
	m_simparams.xsph = false;
	m_simparams.dtadapt = true;
	m_simparams.dtadaptfactor = 0.3;
	m_simparams.buildneibsfreq = 10;
	m_simparams.shepardfreq = 0;
	m_simparams.mlsfreq = 0;
	//m_simparams.visctype = ARTVISC;
	m_simparams.visctype = DYNAMICVISC;
	m_simparams.boundarytype= SA_BOUNDARY;
	m_simparams.tend = 4.0;

	// Free surface detection
	m_simparams.surfaceparticle = false;
	m_simparams.savenormals = false;

	// We have no moving boundary
	m_simparams.mbcallback = false;

	// Physical parameters
	m_physparams.gravity = make_float3(0.0, 0.0, -9.81);
	float g = length(m_physparams.gravity);
	m_physparams.set_density(0, 1800.0, 7.0, 10);

	//set p1coeff,p2coeff, epsxsph here if different from 12.,6., 0.5
	m_physparams.dcoeff = 5.0*g*H;
	m_physparams.r0 = m_deltap;

	// BC when using MK boundary condition: Coupled with m_simsparams.boundarytype=MK_BOUNDARY
	#define MK_par 2
	m_physparams.MK_K = g*H;
	m_physparams.MK_d = 1.1*m_deltap/MK_par;
	m_physparams.MK_beta = MK_par;
	#undef MK_par

	m_physparams.kinematicvisc = 0.125;
	m_physparams.artvisccoeff = 400;
	m_physparams.epsartvisc = 0.01*m_simparams.slength*m_simparams.slength;

	// Allocate data for floating bodies
	allocate_ODE_bodies(1);
	dInitODE();				// Initialize ODE
	m_ODEWorld = dWorldCreate();	// Create a dynamic world
	m_ODESpace = dHashSpaceCreate(0);
	m_ODEJointGroup = dJointGroupCreate(0);
	dWorldSetGravity(m_ODEWorld, m_physparams.gravity.x, m_physparams.gravity.y, m_physparams.gravity.z);	// Set gravity（x, y, z)

	// Drawing and saving times
	set_timer_tick( 0.001f);
	add_writer(VTKWRITER, 5);

	// Name of problem used for directory creation
	m_name = "Trial";
}


Trial::~Trial(void)
{
	release_memory();
	dWorldDestroy(m_ODEWorld);
	dCloseODE();
}


void Trial::release_memory(void)
{
	parts.clear();
	obstacle_parts.clear();
	boundary_parts.clear();
}


int Trial::fill_parts()
{
	float r0 = m_physparams.r0;

	Cube fluid, fluid1;

	experiment_box = Cube(Point(0, 0, 0), Vector(lx, 0, 0),
						Vector(0, ly, 0), Vector(0, 0, lz));
	planes[0] = dCreatePlane(m_ODESpace, 0.0, 0.0, 1.0, 0.0);
	planes[1] = dCreatePlane(m_ODESpace, 1.0, 0.0, 0.0, 0.0);
	planes[2] = dCreatePlane(m_ODESpace, -1.0, 0.0, 0.0, -lx);
	planes[3] = dCreatePlane(m_ODESpace, 0.0, 1.0, 0.0, 0.0);
	planes[4] = dCreatePlane(m_ODESpace, 0.0, -1.0, 0.0, -ly);

	//obstacle = Cube(Point(0.6, 0.24, 2*r0), Vector(0.12, 0, 0),
	//				Vector(0, 0.12, 0), Vector(0, 0, 0.7*lz - 2*r0));
	experiment_box.SetPartMass(r0, m_physparams.rho0[0]);
	if(!m_usePlanes){
		if(m_simparams.boundarytype == SA_BOUNDARY) {
			experiment_box.FillBorder(boundary_parts, boundary_elems, vertex_parts, vertex_indexes, r0, false); // the last parameters is a boolean one to determine if the top face to be filled or not. (false = open)
		}
		else {
			experiment_box.FillBorder(boundary_parts, r0, false);
		}
	}


	fluid = Cube(Point(r0, r0, r0), Vector(lx - 2*r0, 0, 0),
				Vector(0, ly - 2*r0, 0), Vector(0, 0, H - r0));

	if (wet) {
		fluid1 = Cube(Point(H + m_deltap + r0 , r0, r0), Vector(lx - H - m_deltap - 2*r0, 0, 0),
					Vector(0, 0.67 - 2*r0, 0), Vector(0, 0, 0.1));
	}

	boundary_parts.reserve(2000);
	parts.reserve(14000);

	
	

	obstacle.SetPartMass(r0, m_physparams.rho0[0]*0.1);
	obstacle.SetMass(r0, m_physparams.rho0[0]*0.1);
	//obstacle.FillBorder(obstacle.GetParts(), r0, true);
	//obstacle.ODEBodyCreate(m_ODEWorld, m_deltap);
	//obstacle.ODEGeomCreate(m_ODESpace, m_deltap);
	//add_ODE_body(&obstacle);

	fluid.SetPartMass(m_deltap, m_physparams.rho0[0]);
	fluid.Fill(parts, m_deltap, true);
	if (wet) {
		fluid1.SetPartMass(m_deltap, m_physparams.rho0[0]);
		fluid1.Fill(parts, m_deltap, true);
		obstacle.Unfill(parts, r0);
	}

	
	// Rigid body #2 : cylinder
	cylinder = Cylinder(Point(0.5*lx, 0.5*ly, 2.0), 0.025, Vector(0, 0, 0.5));
	cylinder.SetPartMass(8.3f);
	cylinder.SetMass(8.3f);
	cylinder.Unfill(parts, r0);
	cylinder.FillBorder(cylinder.GetParts(), r0);
	cylinder.ODEBodyCreate(m_ODEWorld, m_deltap);
	cylinder.ODEGeomCreate(m_ODESpace, m_deltap);
	add_ODE_body(&cylinder);

	/*joint = dJointCreateHinge(m_ODEWorld, 0);				// Create a hinge joint
	dJointAttach(joint, obstacle.m_ODEBody, 0);		// Attach joint to bodies
	dJointSetHingeAnchor(joint, 0.7, 0.24, 2*r0);	// Set a joint anchor
	dJointSetHingeAxis(joint, 0, 1, 0);*/

	return parts.size() + boundary_parts.size() + obstacle_parts.size() + get_ODE_bodies_numparts();
}

uint Trial::fill_planes() // where is the source?
{
	return (m_usePlanes ? 5 : 0);
}

void Trial::copy_planes(float4 *planes, float *planediv)
{
	if (!m_usePlanes) return;
	
	planes[0] = make_float4(0, 0, 1.0, 0.0);		// bottom plane
	planediv[0] = 1.0;
	planes[1] = make_float4(0, 1.0, 0, 0.0);		// side plane (y near)
	planediv[1] = 1.0;
	planes[2] = make_float4(0, -1.0, 0, ly);	// side plane (y far)
	planediv[2] = 1.0;
	planes[3] = make_float4(1.0, 0, 0, 0.0);		// side plane (x near)
	planediv[3] = 1.0;
	planes[4] = make_float4(-1.0, 0, 0, lx);	// side plane (x far)
	planediv[4] = 1.0;
}


void Trial::ODE_near_callback(void *data, dGeomID o1, dGeomID o2)
{
	const int N = 10;
	dContact contact[N];

	int n = dCollide(o1, o2, N, &contact[0].geom, sizeof(dContact));
	
	for (int i = 0; i < n; i++) {
		contact[i].surface.mode = dContactBounce;
		contact[i].surface.mu   = dInfinity;
		contact[i].surface.bounce     = 0.0; // (0.0~1.0) restitution parameter
		contact[i].surface.bounce_vel = 0.0; // minimum incoming velocity for bounce
		dJointID c = dJointCreateContact(m_ODEWorld, m_ODEJointGroup, &contact[i]);
		dJointAttach (c, dGeomGetBody(contact[i].geom.g1), dGeomGetBody(contact[i].geom.g2));
	}
}


void Trial::copy_to_array(BufferList &buffers)
{
	float4 *pos = buffers.getData<BUFFER_POS>();
	hashKey *hash = buffers.getData<BUFFER_HASH>();
	float4 *vel = buffers.getData<BUFFER_VEL>();
	particleinfo *info = buffers.getData<BUFFER_INFO>();

	std::cout << "Boundary parts: " << boundary_parts.size() << "\n";
	for (uint i = 0; i < boundary_parts.size(); i++) {
		vel[i] = make_float4(0, 0, 0, m_physparams.rho0[0]);
		info[i] = make_particleinfo(BOUNDPART, 0, i);
		calc_localpos_and_hash(boundary_parts[i], info[i], pos[i], hash[i]);
	}
	int j = boundary_parts.size();
	std::cout << "Boundary part mass:" << pos[j-1].w << "\n";

	for (uint k = 0; k < m_simparams.numODEbodies; k++) {
		PointVect & rbparts = get_ODE_body(k)->GetParts();
		std::cout << "Rigid body " << k << ": " << rbparts.size() << " particles ";
		for (uint i = j; i < j + rbparts.size(); i++) {
			vel[i] = make_float4(0, 0, 0, m_physparams.rho0[0]);
			info[i] = make_particleinfo(OBJECTPART, k, i - j);
			calc_localpos_and_hash(rbparts[i - j], info[i], pos[i], hash[i]);
		}
		j += rbparts.size();
		std::cout << ", part mass: " << pos[j-1].w << "\n";
	}

	std::cout << "Obstacle parts: " << obstacle_parts.size() << "\n";
	for (uint i = j; i < j + obstacle_parts.size(); i++) {
		vel[i] = make_float4(0, 0, 0, m_physparams.rho0[0]);
		info[i] = make_particleinfo(BOUNDPART, 1, i);
		calc_localpos_and_hash(obstacle_parts[i-j], info[i], pos[i], hash[i]);
	}
	j += obstacle_parts.size();
	std::cout << "Obstacle part mass:" << pos[j-1].w << "\n";

	std::cout << "Fluid parts: " << parts.size() << "\n";
	for (uint i = j; i < j + parts.size(); i++) {
		vel[i] = make_float4(0, 0, 0, m_physparams.rho0[0]);
		info[i] = make_particleinfo(FLUIDPART, 0, i);
		calc_localpos_and_hash(parts[i-j], info[i], pos[i], hash[i]);
	}
	j += parts.size();
	std::cout << "Fluid part mass:" << pos[j-1].w << "\n";
}
