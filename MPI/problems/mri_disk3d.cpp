namespace fvmhd3d 
{

#define GM     1.0
#define GM_EPS 0.1
#define HoR    0.1

#if 1
#define BND_RADIUS0 0.5
#define BND_RADIUS1 1.0
#define Rin  1.0
#define B_Rin  2.0
#define B_Rout 3.0
#define BND_RADIUS 1.0
#else
#define BND_RADIUS0 0.125
#define BND_RADIUS1 0.75
#define Rin  0.25
#define B_Rin  2.0
#define B_Rout 3.0
#define BND_RADIUS 0.25
#endif

#define Rout 11.0

#define DVAC 1.0e-8


#define _CYLINDER_

  inline real get_r2(const vec3 &pos) 
  {
#ifndef _CYLINDER_
    return pos.norm2();
#else
    return sqr(pos.x) + sqr(pos.y);
#endif
  }

#define _CIRCLE_MOTION_

  bool system::compute_pvel1()
  {
    const int nactive_site = site_active_list.size();
    for (int isite = 0; isite < nactive_site; isite++)
    {
      const int i = site_active_list[isite];

      Particle &p = ptcl_import[i];

			const Fluid W = U_import[i].to_primitive(ptcl_import[i].volume);
      
      p.vel = vec3(W[Fluid::VELX], W[Fluid::VELY], W[Fluid::VELZ]);

      // if density below DVAC, particle are fixed in space (modulo Lloyd regularization)
#if 1
      const real D  = std::log(W[Fluid::DENS]);
      const real DV = std::log(DVAC);
      const real dD = DV * 0.01/2;
      const real r  = (D-DV)/std::abs(dD);
      const real vfac = (-r > 20) ? 0.0 : 1.0/(1.0 + std::exp(-r));
      assert(vfac >= 0.0);
      assert(vfac <= 1.0);
      p.vel *= vfac;
#endif

      const real B2   = sqr(W[Fluid::BX]) + sqr(W[Fluid::BY]) + sqr(W[Fluid::BZ]);
      const real pres = compute_pressure(W);

      const real cs2  = (gamma_gas * pres + B2)/W[Fluid::DENS];
      const real vel2 = vec3(W[Fluid::VELX], W[Fluid::VELY], W[Fluid::VELZ]).norm2();
      const real vabs = std::sqrt(cs2 + 1.0*vel2);

      const vec3 centroid = cell_list[i].centroid - p.pos;
      const real d = centroid.abs();
      if (d == 0.0) continue;

      const real eta = 0.25f;
      const real ki  = 1.0f;

      const real f1  = 0.9f;
      const real f2  = 1.1f;

      const real R   = std::pow(cell_list[i].Volume * (3.0/(4.0*M_PI)), 1.0/3.0);
      const real fac = d/(eta*R);

      real f;
      if      (fac < f1) f = 0.0;
      else if (fac < f2) f = (d - f1*eta*R)/((f2 - f1)*eta*R);
      else               f = 1.0;

      real tau = d / vabs;
      f *= ki/tau;
      p.vel += centroid*f;

#ifdef _CIRCLE_MOTION_
     
      p.vel = p.vel - (p.vel*p.pos)*p.pos/p.pos.norm2();  // subtract radial component

#else
      
      if (p.pos.abs() < BND_RADIUS) p.vel = 0.0;
      if (p.is_boundary())          p.vel = 0.0;

      if (p.idx > 0 && p.is_boundary() && p.pos.abs() > 0.25*BND_RADIUS && dt_global > 0.0)
      {
        const real dt = 32.0*dt_global;
        const real h = std::pow(p.volume/(4.0*M_PI/3.0), 1.0/3.0);
        p.vel = -p.pos * (h/dt * 1.0/p.pos.abs())*drand48();
      }

#endif

    }
    return true;
  }

  std::pair<vec3, real> gacc(const vec3 &pos)
  {
    const real ds2  = pos.norm2();
    const real ds   = std::sqrt(ds2);
    const real ids  = (ds > 0.0) ? 1.0/ds : 0.0;
    const real ids2 = ids*ids;

    const real gravity_mass = GM;
    const real gravity_eps  = GM_EPS;
    assert(gravity_eps > 0.0);
    const real h  = gravity_eps;
    const real q  = ds/h;
    const real q2 = q*q;
    const real q3 = q2*q;
    const real q4 = q2*q2;
    const real q5 = q3*q2;

    real pot;
    real acc;
    if (q < 1) 
    {
      acc = -1.0/sqr(h) * (4.0/3*q  - 6.0/5*q3 + 0.5f*q4);
      pot = +1.0/    h  * (2.0/3*q2 - 0.3f *q4 + 0.1f*q5 - 7.0/5);
    } 
    else if (q < 2) 
    {
      acc = -1.0/sqr(h) * (8.0/3*q - 3.0*q2 + 6.0/5*q3 - 1.0/6*q4 - 1.0/15/q2);
      pot = +1.0/    h  * (4.0/3*q2 - q3 + 0.3*q4 - 1.0/30*q5 - 8.0/5 + 1.0/15/q);
    } 
    else 
    {
      acc = -ids2;
      pot = -ids;
    }

    acc *= gravity_mass*ids;
    pot *= gravity_mass;

    return std::make_pair(acc * pos, pot);
  }

#ifdef _CIRCLE_MOTION_
  const vec3 project(const vec3 &r, const vec3 &v)
  {
    return (v - r*((r*v)/r.norm2()));
  }
  const std::pair<vec3, vec3> rotate(const vec3 &r, const vec3 &vt, const real dt)
  {
    const real th = vt.abs()*dt/r.abs();

    const real sinth = sin(th);
    const real costh = cos(th);

    const vec3 rv = r.cross(vt);
    const vec3 n = rv/rv.abs();
    const vec3 n2(sqr(n.x), sqr(n.y), sqr(n.z));

    const real Axx = n2.x + (n2.y + n2.z)*costh;
    const real Ayy = n2.y + (n2.x + n2.z)*costh;
    const real Azz = n2.z + (n2.x + n2.y)*costh;
    const real Axy = n.x*n.y*(1-costh) - n.z*sinth;
    const real Axz = n.x*n.z*(1-costh) + n.y*sinth; 
    const real Ayx = n.x*n.y*(1-costh) + n.z*sinth;
    const real Ayz = n.y*n.z*(1-costh) - n.x*sinth;
    const real Azx = n.x*n.z*(1-costh) - n.y*sinth;
    const real Azy = n.y*n.z*(1-costh) + n.x*sinth;

    return std::make_pair(
        vec3(
          vec3(Axx, Axy, Axz)*r,
          vec3(Ayx, Ayy, Ayz)*r,
          vec3(Azx, Azy, Azz)*r),
        vec3(
          vec3(Axx, Axy, Axz)*vt,
          vec3(Ayx, Ayy, Ayz)*vt,
          vec3(Azx, Azy, Azz)*vt)
        );
  }

  void system::predict(const int i)
  {
    assert(i >= 0);
    Particle &p = ptcl_local[i];

    const std::pair<vec3, real> f0 = gacc(p.orig_pos);
    p.acc0 = f0.first;
    
    const real dt     = t_global - p.tlast;
    p.pos = p.orig_pos;
    p.vel = p.orig_vel; 

    if (p.orig_pos.cross(p.orig_vel).norm2() > 0.0)
    {
      const std::pair<vec3,vec3> rv1 = rotate(p.orig_pos, p.orig_vel, dt);
      p.pos = rv1.first;
    }
    
    const std::pair<vec3, real> f1 = gacc(p.pos);
    p.acc1 = f1.first;
  }

  void system::correct(const int i0)
  {
    const int i = i0 < 0 ? -1-i0 : i0;
    Particle &p = ptcl_local[i];
      
    if (p.is_boundary())
    {
      p.tlast = scheduler.get_tsys();
      p.tend  = scheduler.get_tsys() + scheduler.get_dt(p.rung);

      p.unset_remove();
      p.orig_pos = p.pos;
      p.orig_vel = p.vel;
      if (p.idx > 0 && p.pos.abs() < 0.975*BND_RADIUS) p.set_remove();
      problem_set_boundary(i, p.volume);
      return;
    }

    p.orig_pos = p.pos;
    p.orig_vel = p.vel;
    p.tlast = scheduler.get_tsys();
    p.tend  = scheduler.get_tsys() + scheduler.get_dt(p.rung);
  }

#else

  void system::predict(const int i)
  {
    assert(i >= 0);
    Particle &p = ptcl_local[i];
    assert(p.idx >= 0);
    const real dth = 0.5*(p.tend - p.tlast);
    const std::pair<vec3, real> fh = gacc(p.orig_pos + p.orig_vel*dth);
    p.acc0 = p.acc1 = fh.first;

    const real dt     = t_global - p.tlast;
    p.vel = p.orig_vel; 
    p.pos = p.orig_pos + p.orig_vel*dt;

    if (p.is_active() && (p.pos.abs() < BND_RADIUS))
    {
      if (!p.is_boundary()) boundary_n++;
      p.boundary = Particle::DIOD;
    }
  }

  void system::correct(const int i0)
  {
    const int i = i0 < 0 ? -1-i0 : i0;
    Particle &p = ptcl_local[i];
    assert(p.idx >= 0);
    {
      if (p.is_boundary())
      {
        p.tlast = scheduler.get_tsys();
        p.tend  = scheduler.get_tsys() + scheduler.get_dt(p.rung);

        p.unset_remove();
        p.orig_pos = p.pos;
        p.orig_vel = p.vel;
        if (p.idx > 0 && p.pos.abs() < 0.975*BND_RADIUS) p.set_remove();
        problem_set_boundary(i, p.volume);
        return;
      }
      else
        assert(p.pos.abs() >= BND_RADIUS);

      const real dth = 0.5*(t_global - p.tlast);
      const vec3 ipos = p.orig_pos + (p.vel + p.orig_vel)*dth;
      const real r0 = (ipos - p.pos).abs();
      const real h = std::pow(p.volume/(4.0*M_PI/3.0), 1.0/3.0);
      if (r0 < 0.25*h) p.orig_pos = ipos;
      else             p.orig_pos = p.pos;

      p.orig_vel = p.vel;
      const std::pair<vec3, real> f = gacc(p.orig_pos);
      p.acc0 = p.acc1 = f.first;
      p.pot = f.second;

      p.tlast = scheduler.get_tsys();
      p.tend  = scheduler.get_tsys() + scheduler.get_dt(p.rung);
    }
  }

#endif

  void system::problem_set_boundary(const int i, const real v)
  {
    Particle &p = ptcl_local[i];
    if (!p.is_boundary()) return;
    assert(i == p.local_id);
    p.volume = v;
    U_local[p.local_id][Fluid::MOMX] = 0.0;
    U_local[p.local_id][Fluid::MOMY] = 0.0;
    U_local[p.local_id][Fluid::MOMZ] = 0.0;
    U_local[p.local_id][Fluid::WBX] = 0.0;
    U_local[p.local_id][Fluid::WBY] = 0.0;
    U_local[p.local_id][Fluid::WBZ] = 0.0;
    U_local[p.local_id][Fluid::DENS] = DVAC*p.volume;
    U_local[p.local_id][Fluid::ETHM] = DVAC*p.volume;
    return;
  }


  bool system::compute_update_prob(Fluid &U, const int i0)
  {
    const int i = (i0 < 0) ? -1-i0 : i0;

    if (i0 < 0)
    {
      const Fluid &W = U;

      const real DFLOOR = DVAC;
      const real R = ptcl_import[i].pos.abs();
      if (
          W[Fluid::DENS] <= 1.001*DFLOOR
          || R > 15.5
          || R < 1.1*BND_RADIUS
         )
      {
        Wrec_import[i].t = 0.0;
        Wrec_import[i].x = 0.0;
        Wrec_import[i].y = 0.0;
        Wrec_import[i].z = 0.0;
      }
    }

    return false;
  }

  bool system::problem_force_distribute()
  {
#if 1
    unsigned long long boundary_glb;
    MPI_Allreduce(&boundary_n, &boundary_glb, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    return (boundary_glb > 20000);
#else
    return false;
#endif
  }
  const real system::extra_timestep(const int i)
  {
    const Particle &p = ptcl_import[i];
    if (p.is_boundary()) return HUGE;
    const real r  = p.pos.abs();
    return courant_no*r/(1.0/HUGE + p.vel.abs());
  }


  inline real get_cs2(const vec3 r)
  {
    return (GM/std::sqrt(get_r2(r)))*sqr(HoR);
  }

  real system::compute_ethm_update(const Fluid &W, const int i) const
  {
    return get_cs2(ptcl_import[i].pos)*W[Fluid::DENS];
  }
  real system::compute_pressure(const Fluid& W) const
  {
    return W[Fluid::ETHM];
  }
  real system::compute_entropy_from_ethm(const Fluid& W) const
  {
    return 1.0;
  }
  real system::compute_ethm_from_entropy(const Fluid& W) const
  {
    assert(false);
    return -1.0;
  }

  bool system::refine(const int i) 
  {
    return false;
  }
  bool system::derefine(const int i)
  {
    return false;
  }

  void system::set_geometry(const bool init) 
  {
    const double dt_max = 1.0;
    scheduler = Scheduler(dt_max);

    t_end   = 2500.0 + dt_max;

    n_restart = 1;
    dt_restart = HUGE; // dt_max;

    dt_dump = 1.0;

    di_log = 100;

    global_n = local_n = 0;


    const double Lbox = 32;
    const vec3 rmin(-Lbox/2.0, -Lbox/2.0, -Lbox/2.0);
    const vec3 rmax(+Lbox/2.0, +Lbox/2.0, +Lbox/2.0);
    global_domain = boundary(rmin, rmax);
    global_domain_size = global_domain.hsize() * 2.0;

    const vec3 Len3 = global_domain.hsize() * 2.0;
    pfloat<0>::set_scale(Len3.x);
    pfloat<1>::set_scale(Len3.y);
    pfloat<2>::set_scale(Len3.z);

    Distribute::int3 nt(1, 1, 1);
    switch(nproc) 
    {
      case 1: break;
      case 2: nt.x = 2; nt.y = 1; nt.z = 1; break;
      case 4: nt.x = 2; nt.y = 2; nt.z = 1; break;
      case 8: nt.x = 4; nt.y = 2; nt.z = 1; break;
      case 16: nt.x = 4; nt.y = 4; nt.z = 1; break;
      case 32: nt.x = 8; nt.y = 4; nt.z = 1; break;
      case 64: nt.x = 8; nt.y = 8; nt.z = 1; break;
      case 128: nt.x = 8; nt.y = 8; nt.z = 2; break;
      case 256: nt.x = 8; nt.y = 8; nt.z = 4; break;
      default: assert(false);
    }

    const Distribute::int3 nt_glb(nt);
    const pBoundary pglobal_domain(pfloat3(0.0), pfloat3(Len3));
    distribute_glb.set(nproc, nt, pglobal_domain);

    if (!init) return;

    if (myproc == 0) 
    {

      int Namb  = 0.5e5;
      int Ndisk = 1.5e5;

#if 1
      Namb  = 0.5e6;
      Ndisk = 1.5e6;
#endif

      ptcl_local.clear();
      ptcl_local.reserve(128);

      fprintf(stderr, "Ndisk= %d \n", Ndisk);
      const real dr_amb  = std::pow(Lbox*Lbox*Lbox/Namb,      1.0/3.0);
      fprintf(stderr, "dr_amb=  %g \n", dr_amb );
      fprintf(stderr, "rmin= %g %g %g \n", 
          global_domain.get_rmin().x,
          global_domain.get_rmin().y,
          global_domain.get_rmin().z);
      fprintf(stderr, "rmax= %g %g %g \n", 
          global_domain.get_rmax().x,
          global_domain.get_rmax().y,
          global_domain.get_rmax().z);
      fprintf(stderr, "\n -------------------------- \n\n");

      const int N_per_ann = 100;
      const int Nann = Ndisk/N_per_ann;
      const real eps = 
        std::pow(Rout/Rin, 
            1.0/(real)Nann);

      for (int j = 0; j < Nann; j++)
      {
        const real R0 = Rin * std::pow(eps, (real)(j+0));
        const real R1 = Rin * std::pow(eps, (real)(j+1));
        const real dR = R1 - R0;
        const real sig_slope = +1.99f;
        const real mass_ratio = 
          (std::pow(R1,   2.0 - sig_slope) - std::pow(R0,  2.0 - sig_slope))/
          (std::pow(Rout, 2.0 - sig_slope) - std::pow(Rin, 2.0 - sig_slope));
        const int Nj = (int)(mass_ratio * Ndisk); 

        for (int i = 0; i < Nj; i++)
        {
          const real ecc = 0.0;
          real sma, phi, R;
          bool flag = true;

          while (flag)
          {

            // SEMI-MAJOR AXIS
            //
            sma  = R0 + dR * drand48();
            phi = 2*M_PI * drand48();

            // CONVERT SEMI-MAJOR AXIS TO RADIUS
            R = sma * ((1.0-sqr(ecc))/(1.0 + (ecc*cos(phi))));

            const real area_scale = R / (sma*(1.0+ecc));
            const real scale    = area_scale;
            const real fv       = drand48();

            if (fv <= scale) flag = false;
          }

          const real H = 1.0 *  HoR * R;
          real z;
          flag = true;
          while (flag)
          {
            z = (2.0*drand48() - 1.0)*R;
            const real fz = drand48();
            const real scale = exp(-sqr(z)/2.0/sqr(H));
            if (fz <= scale) flag = false;
          }

          float h = std::pow(M_PI*(sqr(R1) - sqr(R0))*2*H/Nj, 1.0/3);

          const dvec3 vel(0.0, 0.0, 0.0);
          const dvec3 pos(R*cos(phi), R*sin(phi), z);
          Particle p;
          p.set_pos(pos);
          p.vel = vel;
          p.orig_vel = p.vel;
          p.boundary = 0;
          p.idx = ptcl_local.size() + 1;
          p.rmax = 2.0*h;
          ptcl_local.push_back(p);
        }
      }

#if 0
      for (int i = 0; i < Namb; i++)
      {
        const vec3 pos = global_domain.rmin + dvec3(
            drand48()*Len3.x,
            drand48()*Len3.y,
            drand48()*Len3.z);

        dvec3 vel(0.0, 0.0, 0.0);
        Particle p;
        p.set_pos(pos);
        p.vel = vel;
        p.orig_vel = p.vel;
        p.boundary = 0;
        p.idx = i + 1;
        p.rmax = 2.0*dr_amb;
        ptcl_local.push_back(p);
      }
#endif

#if 1
      {
        int pc = 0;
        vec3 pos(HUGE/100);
        while (pc < Namb)
        {
          pos.x = (1.0 - 2.0*drand48()) * Lbox/2.0;
          pos.y = (1.0 - 2.0*drand48()) * Lbox/2.0;
          pos.z = (1.0 - 2.0*drand48()) * Lbox/2.0;
          const vec3 vel(0.0, 0.0, 0.0);
          Particle p;
          p.set_pos(pos);
          p.vel = vel;
          p.orig_vel = p.vel;
          p.boundary = 0;
          p.idx = Ndisk*100+pc+1;
          const real rmax = 1.5*Lbox/std::pow(Namb, 1.0/3.0);
          p.rmax = rmax;

          ptcl_local.push_back(p);
          pc++;
          if (pc%100000 == 0)
            fprintf(stderr, "bnd: i= %d \n", pc);
        }
      }
#endif

#if 0
      {
        int pc = 0;

        const real Rmax = Len3.abs();
        const real eps  = BND_RADIUS1;
        const real pow  = 3.0/2.0;
        assert(pow > 1.0);
        const real r0   = eps/std::sqrt(pow - 1.0);
        const real n3rmax = r0*r0/std::pow(r0*r0 + eps*eps, pow);

        while (pc < Namb)
        {
          bool pick = false;
          real r = eps;
          while (!pick)
          {
            r   = Rmax*drand48();
            if (r < BND_RADIUS0) continue;
            const real n3  = 1.0/std::pow(r*r + eps*eps, pow);
            const real n3r = r*r*n3;
            pick = (n3rmax*drand48() < n3r);
          }
#if 1
          const real theta = acos(1.0 - 2.0*drand48());
#else
          real theta = 0;
          pick = false;
          while (!pick)
          {
            theta = drand48()*M_PI;
            const real sigma = (M_PI/2.0) * 0.075;
            const real f= 0.05;
            const real p = (f + (1.0 - f)*exp(-sqr(theta - M_PI/2.0)/2/sqr(sigma)))*sin(theta);
            pick = (drand48() < p);
          }
#endif
          const real phi   = 2*M_PI*drand48();
          const vec3 pos(r*sin(theta)*cos(phi), r*sin(theta)*sin(phi), r*cos(theta));
          if (pos.x < global_domain.get_rmin().x) continue;
          if (pos.x > global_domain.get_rmax().x) continue;
          if (pos.y < global_domain.get_rmin().y) continue;
          if (pos.y > global_domain.get_rmax().y) continue;
          if (pos.z < global_domain.get_rmin().z) continue;
          if (pos.z > global_domain.get_rmax().z) continue;
          const vec3 vel(0.0, 0.0, 0.0);
          Particle p;
          p.set_pos(pos);
          p.vel = vel;
          p.orig_vel = p.vel;
          p.boundary = 0;
          p.idx = ptcl_local.size() + Ndisk*10 + 1;
          const real rmax = 0.1*r;
          p.rmax = rmax;

          ptcl_local.push_back(p);
          if (pc%100000 == 0)
            fprintf(stderr, "i= %d \n", pc);
          pc++;
        }
      }
#endif



      local_n  = ptcl_local.size();
      global_n = local_n;

      fprintf(stderr, "  *** proc= %d : local_n= %llu  global_n= %llu \n", myproc, local_n, global_n);
    } // myproc == 0

    MPI_Bcast(&global_n,  1, MPI_INT, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    if (myproc == 0)
      fprintf(stderr, " ***  Distrubiting data \n");

    all_active = true;

    for (int k = 0; k < 5; k++)
      distribute_data(false, false,false);

    fit_vec(ptcl_local);

    MPI_Barrier(MPI_COMM_WORLD);
    fprintf(stderr, " *** proc= %d : local_n= %llu  global_n= %llu \n", myproc, local_n, global_n);
    MPI_Barrier(MPI_COMM_WORLD);

    if (myproc == 0)
      fprintf(stderr, " --- relax -- \n");
    relax_mesh(2);
    if (myproc == 0)
      fprintf(stderr, " ---- done --- \n");
    {
      distribute_data(false, false, false);
      const double t10 = mytimer::get_wtime();
      clear_mesh(false);
      int nattempt = build_mesh_global();
      double dt10 = mytimer::get_wtime() - t10;

      double volume_loc = 0.0;
      {
        std::vector<TREAL> v(local_n);
        for (int i = 0; i < (int)local_n; i++)
          v[i] = cell_local[i].Volume;
        std::sort(v.begin(), v.end());  // sort volumes from low to high, to avoid roundoff errors
        for (int i = 0; i < (int)local_n; i++)
          volume_loc += v[i];
      }

      double dt10max;
      MPI_Allreduce(&dt10, &dt10max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
      double volume_glob = 0.0;	
      int    nattempt_max, nattempt_min;
      MPI_Allreduce(&volume_loc, &volume_glob,  1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      MPI_Allreduce(&nattempt,   &nattempt_max, 1, MPI_INT,    MPI_MAX, MPI_COMM_WORLD);
      MPI_Allreduce(&nattempt,   &nattempt_min, 1, MPI_INT,    MPI_MIN, MPI_COMM_WORLD);

      const double volume_exact = global_domain_size.x*global_domain_size.y*global_domain_size.z;
      if (myproc == 0)
      {
        fprintf(stderr, "first call build_mesh:[ %g  sec ::  %g cells/s/proc/thread ]\n",
            dt10max,
            global_n/nproc/dt10max);
        fprintf(stderr, "   computed_volume= %g  exact_volume= %g diff= %g [ %g ]  nattempt= %d %d \n",
            volume_glob, volume_exact, 
            volume_glob - volume_exact,	(volume_glob - volume_exact)/volume_exact,
            nattempt_min, nattempt_max);
      }
    }


    extract_ngb_from_mesh();
  }

  void system::set_problem(const bool init) 
  {

    if (myproc == 0)
    {
      fprintf(stderr, " ********* Setting up mri_disk3d problem ************* \n");
      fprintf(stderr, " GM= %g \n", GM);
      fprintf(stderr, " GM_EPS= %g \n", GM_EPS);
      fprintf(stderr, " HoR= %g \n", HoR);
      fprintf(stderr, " Rin= %g \n", Rin);
      fprintf(stderr, " Rout= %g \n", Rout);
    }


    gamma_gas  = 1.0;
    courant_no = 0.8;

    if (!init) return;

    const real D0       = 1.0;
    const real DENS_MIN = DVAC;

    U_local.resize(local_n);
    dU_local.resize(local_n);
    Wrec_local.resize(local_n);

    for (int i = 0; i < (int)local_n; i++) 
    {
      Particle &pi = ptcl_local[i];

      real x = pi.pos.x;
      real y = pi.pos.y;
      real z = pi.pos.z;

      const real R  = std::sqrt(get_r2(pi.pos)); // std::sqrt(sqr(pi.pos.x) + sqr(pi.pos.y));
      assert(R > 0.0);

      if (pi.pos.abs() < BND_RADIUS)
      {
        pi.idx = 0;
        pi.boundary = Particle::DIOD;
      }
      else
      {
        pi.boundary = 0;
      }

      const real H = HoR * R;

      const real vphi = std::sqrt(GM/R * (1 - sqr(H/R)));
      real vx = -vphi * y/R;
      real vy = +vphi * x/R;
      real vz = 0.0;
      if (R > Rout)
        vx = vy = vz = 0;

      const real dv = vphi * 1.0e-4;
      vx += drand48() * dv;
      vy += drand48() * dv;

      real bx = 0.0;
      real by = 0.0;
      real bz = 0.0;




      real dmid = D0*std::pow(x*x+y*y, -3.0/4.0);
      if (R > Rout)
        dmid = DENS_MIN;

      real dens = exp(-sqr(z)/2/sqr(H)) * dmid;
      if (dens < DENS_MIN)
      {
        vx = drand48() * dv;
        vy = drand48() * dv;
        dens =  DENS_MIN;
      }

      //      const real pmid = sqr(vphi*HoR)*dmid;
      const real pres = sqr(vphi*HoR)*dens;

      real scalar = 1;

      const real beta = 25.0;
      const real bf = 1.0*std::sqrt(2.0*pres/beta);

      bx = -bf * y/R;
      by = +bf * x/R;
      bz = 0.0;
      if (R < Rin*1.1 || R > 0.9*Rout || std::abs(z) > 2.0*HoR*R)
        bx = by = bz = 0;

      Fluid m;

      m[Fluid::DENS] = dens;
      m[Fluid::ETHM] = pres;
      m[Fluid::VELX] = vx;
      m[Fluid::VELY] = vy;
      m[Fluid::VELZ] = vz;
      m[Fluid::BX  ] = bx;
      m[Fluid::BY  ] = by;
      m[Fluid::BZ  ] = bz;
      m[Fluid::PSI ] = 0.0;
      m[Fluid::ENTR] = 1.0;
      for (int k = 0 ; k < Fluid::NSCALARS; k++)
        m.scal(k) = scalar;

      Wrec_local[i]        = Fluid_rec(m);
      U_local  [i]       = m.to_conservative(cell_local[i].Volume);
      dU_local[i] = 0.0;
      ptcl_local[i].volume = cell_local[i].Volume;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (myproc == 0)
      fprintf(stderr , " pvel ... \n");

    get_active_ptcl(true);

    MPI_Barrier(MPI_COMM_WORLD);
    if (myproc == 0)
      fprintf(stderr , " pvel ... \n");


    cell_list.swap(cell_local);
    U_import.swap(U_local);
    ptcl_import.swap(ptcl_local);
    site_active_list.swap(active_ptcl);

    compute_pvel();
    compute_timesteps(true);

    cell_list.swap(cell_local);
    U_import.swap(U_local);
    ptcl_import.swap(ptcl_local);
    site_active_list.swap(active_ptcl);

    for (int i = 0; i < (int)local_n; i++)
    {
      ptcl_local[i].rung += 3;
      ptcl_local[i].tend  = 0.0 + scheduler.get_dt(ptcl_local[i].rung);
      ptcl_local[i].orig_vel = ptcl_local[i].vel;
    }
    all_active = true;
    scheduler.flush_list();
    boundary_n = 0;
    for (int i = 0; i < (int)local_n; i++)
    {
      scheduler.push_particle(i, (int)ptcl_local[i].rung);
      if (ptcl_local[i].is_boundary())
        boundary_n++;
      ptcl_local[i].unset_active();
    }

    unsigned long long boundary_glb;
    MPI_Allreduce(&boundary_n, &boundary_glb, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

    if (myproc == 0)
      fprintf(stderr, "boundary_glb= %lld\n", boundary_glb);

    clear_mesh(true);

    MPI_Barrier(MPI_COMM_WORLD);
    if (myproc == 0) fprintf(stderr, " proc= %d: complete problem setup \n", myproc);
  }
}

