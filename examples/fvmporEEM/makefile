cc=icc
CC=icpc
#CC=g++ -Wall -Wno-sign-compare -Wno-unused
localOPTS=-DMPICH_IGNORE_CXX_SEEK -DUSE_MINLIN -DUSE_MKL
debugOPTS=-g -O3
#debugOPTS=-g -O3 -openmp
#debugOPTS=-g -O0 -DLIN_VERBOSE
#debugOPTS=-DMPICH_IGNORE_CXX_SEEK -DFVM_DEBUG -DMESH_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -DVECTOR_DEBUG -DLIN_DEBUG -g -O0 -fno-inline -DLIN_DEBUG -DLIN_VERBOSE
parmsOPTS=-DUSE_MPI -DREAL=double -DDBL -DHAS_BLAS -DFORTRAN_UNDERSCORE -DVOID_POINTER_SIZE_8
RM=rm -f
IS64BIT=defined

ifdef IS64BIT
INCLUDE=-I../../include -I$(HOME)/include -I/opt/parmetis -I/opt/sundials2.4.0/include -I/home/cummingb/projects/minlin -I/usr/local/cuda/include
MKL=-lmkl_intel_lp64 -lmkl_intel_thread -lmkl_core -liomp5
LIBS=-L/opt/parmetis -L/opt/sundials2.4.0/lib -L/usr/local/cuda/lib64
MINLIN=/home/cummingb/projects/minlin
else
INCLUDE=-I../../include -I/opt/intel/impi/3.2/include -I/opt/intel/Compiler/11.1/069/mkl/include -I/home/cummingb/include
MKL=-lmkl_intel_threa
LIBS=-L/opt/intel/impi/3.2/lib -L/opt/intel/Compiler/11.1/069/mkl/lib/32 -L/opt/intel/Compiler/11.1/069/lib/ia32 -L/home/cummingb/lib
endif

SUNDIALS=/opt/sundials2.4.0/lib/libsundials_idas.a /opt/sundials2.4.0/lib/libsundials_nvecparallel.a

LIB=$(MKL) $(SUNDIALS) -lmpi -lpthread -lcublas -lcusparse -lcuda
MESH=../../mesh.o
UTIL=$(MINLIN)/cuda.o

# currently we use the DSS preconditioner, however this won't be needed and you can comment it out and use the blank
# preconditioner below
preconOPTS=-DPRECON_DSS -DPRECON
PRECON=preconditioner_dss.o

#preconOPTS=
#PRECON=

IMPLEMENTATIONDEPS_ODE=fvmpor_ODE.o fvmpor.o shape.o $(MESH) $(UTIL) $(PRECON)

OPTS=$(localOPTS) $(debugOPTS) $(preconOPTS) -openmp

# ...............
# all
# ...............
all: cassion vs

# ................
# compile
# ................
cassion: cassion.h fvmpor_ODE_impl.cpp $(IMPLEMENTATIONDEPS_ODE)
	$(CC) $(OPTS) $(INCLUDE) $(LIBS) -o cassion fvmpor_ODE_impl.cpp -DPROBLEM_CASSION $(IMPLEMENTATIONDEPS_ODE) $(LIB)


vs: vs.h fvmpor_ODE_impl.cpp $(IMPLEMENTATIONDEPS_ODE)
	$(CC) $(OPTS) $(INCLUDE) $(LIBS) -o vs fvmpor_ODE_impl.cpp -DPROBLEM_VS $(IMPLEMENTATIONDEPS_ODE) $(LIB)

# ............
# Object files
# ............
cassion.o : cassion.cpp fvmpor.h
	$(CC) $(OPTS) $(INCLUDE) -c cassion.cpp

fvmpor_ODE.o : fvmpor_ODE.h preconditioner_dss.h fvmpor_ODE.cpp
	$(CC) $(OPTS) $(INCLUDE) -c fvmpor_ODE.cpp -o fvmpor_ODE.o

fvmpor_ODE_impl.o : fvmpor_ODE.h fvmpor_ODE_impl.cpp fvmpor.h
	$(CC) $(OPTS) -DPROBLEM_CASSION $(INCLUDE) -c fvmpor_ODE_impl.cpp -o fvmpor_ODE_impl.o

fvmpor.o : fvmpor.h fvmpor.cpp
	$(CC) $(OPTS) $(INCLUDE) -c fvmpor.cpp -o fvmpor.o

preconditioner_dss.o : preconditioner_dss.h preconditioner_dss.cpp
	$(CC) $(OPTS) $(INCLUDE) -c preconditioner_dss.cpp -o preconditioner_dss.o

shape.o : shape.h shape.cpp
	$(CC) $(OPTS) $(INCLUDE) -c shape.cpp -o shape.o


# ............
# clean
# ............
clean:
	$(RM) cassion
	$(RM) vs
	$(RM) *.o
