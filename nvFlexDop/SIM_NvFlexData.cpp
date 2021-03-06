#include "SIM_NvFlexData.h"
#include <PRM/PRM_Template.h>
#include <PRM/PRM_Default.h>

#include <NvFlexDevice.h>



static void CreateFluidParticleGrid(NvFlexExtParticleData& ptd, int* indices, Vec3 lower, int dimx, int dimy, int dimz, float radius, Vec3 velocity, float invMass, int phase, float jitter = 0.005f);

static uint cudaContextAcquiredCount = 0;

bool acquireCudaContext() {
	if (SIM_NvFlexData::nvFlexLibrary == NULL)return false;
	NvFlexAcquireContext(SIM_NvFlexData::nvFlexLibrary);
	++cudaContextAcquiredCount;
	return true;
}

bool releaseCudaContext() {
	if (SIM_NvFlexData::nvFlexLibrary==NULL || cudaContextAcquiredCount==0)return false;
	NvFlexRestoreContext(SIM_NvFlexData::nvFlexLibrary);
	--cudaContextAcquiredCount;
	return true;
}

void SIM_NvFlexData::initializeSubclass() {
	SIM_Data::initializeSubclass();
	_lastGdpPId = -1;
	_lastGdpTId = -1;

	

	//debug test
	/*float sizex = 1.76f;
	float sizey = 3.20f;
	float sizez = 3.50f;

	float restDistance = 0.055f;

	int x = int(sizex / restDistance);
	int y = int(sizey / restDistance);
	int z = int(sizez / restDistance);

	NvFlexExtParticleData ptd = NvFlexExtMapParticleData(nvdata->container());

	NvFlexExtAllocParticles(nvdata->container(), x*y*z, _indices.get());
	CreateFluidParticleGrid(ptd, _indices.get(), Vec3(0, restDistance, 0), x, y, z, restDistance, Vec3(0.0f), 1, eNvFlexPhaseSelfCollide | eNvFlexPhaseFluid);
	*/

	//NvFlexExtUnmapParticleData(nvdata->container());
}

void SIM_NvFlexData::setParametersSubclass(const SIM_Options & parms) {
	// we dont care what option was set for now, we have only one
	SIM_Data::setParametersSubclass(parms);
	
	int ptsmaxcount = getMaxPtsCount();
	if (_prevMaxPts == ptsmaxcount)return;

	try {
		acquireCudaContext();
		nvdata.reset(new NvFlexContainerWrapper(SIM_NvFlexData::nvFlexLibrary, ptsmaxcount, 0));
		releaseCudaContext();
		_indices.reset(new int[ptsmaxcount]);
	}
	catch (...) {
		std::cout << "nvflex data initialization failed!" << std::endl;
		_valid = false;
		nvdata.reset();
		_indices.reset();
		return;
	}
	_prevMaxPts = ptsmaxcount;
	std::cout << "nvflex data initialized with " << ptsmaxcount << std::endl;
}

void SIM_NvFlexData::makeEqualSubclass(const SIM_Data* source) {
	SIM_Data::makeEqualSubclass(source);
	std::cout << "do makeEqual" << std::endl;
	const SIM_NvFlexData* src = SIM_DATA_CASTCONST(source, SIM_NvFlexData);
	if (src == NULL) {
		// some info?
		std::cout << "makeEqual src==Null" << std::endl;
		return;
	}
	nvdata = src->nvdata;
	_indices = src->_indices;
	_lastGdpPId = src->_lastGdpPId;
	_lastGdpTId = src->_lastGdpTId;
	_prevMaxPts = src->_prevMaxPts;
	_valid = _valid && src->_valid;
	if (!_valid) {
		std::cout << "makeEqual data was invalid" << std::endl;
		nvdata.reset();
		_indices.reset();
	}
}

const SIM_DopDescription* SIM_NvFlexData::getDescriptionForFucktory() {
	static PRM_Name maxpts_name("maxpts", "Maximum Particles Count");

	static PRM_Default maxpts_default(1000000);

	static PRM_Template prms[]{
		PRM_Template(PRM_INT_E, 1, &maxpts_name, &maxpts_default),
		PRM_Template()
	};

	static SIM_DopDescription desc(true, "nvflexData", "NvFlex Data", "NvFlexData", classname(), prms);
	return &desc;
}

static void nvFlexErrorCallbackPrint(NvFlexErrorSeverity type, const char *msg, const char *file, int line) {
	switch (type) {
	case eNvFlexLogError:
		std::cout << "NvF ERROR: "; break;
	case eNvFlexLogWarning:
		std::cout << "NvF WARNING: "; break;
	case eNvFlexLogDebug:
		std::cout << "NvF DEBUG: "; break;
	case eNvFlexLogAll:
		std::cout << "NvF ALL: "; break;
	}
	if (msg != NULL)std::cout << msg;
	std::cout << " :: ";
	if (file != NULL)std::cout << file;
	std::cout << " :: ";
	std::cout << line;
	std::cout << std::endl;

}

//cuda-aware deleter
void delete_NvFlexContainerWrapper(SIM_NvFlexData::NvFlexContainerWrapper *wrp) {
	acquireCudaContext();
	delete wrp;
	releaseCudaContext();
}


SIM_NvFlexData::SIM_NvFlexData(const SIM_DataFactory*fack):SIM_Data(fack),SIM_OptionsUser(this), _indices(nullptr_t(), std::default_delete<int[]>()), nvdata(nullptr_t(), delete_NvFlexContainerWrapper), _lastGdpPId(-1), _lastGdpTId(-1), _lastGdpStrId(-1), _prevMaxPts(-1), _valid(false){
	if (nvFlexLibrary != NULL)_valid = true;
	std::cout << "flex data constructed." << std::endl;
}


SIM_NvFlexData::~SIM_NvFlexData(){
	std::cout << "flex data destructed." << std::endl;
}


//wrapper
bool NvFlexHLibraryHolder::cudaContextCreated = false;
NvFlexLibrary* NvFlexHLibraryHolder::nvFlexLibrary = NULL;
GA_Size NvFlexHLibraryHolder::_instanceCount = 0;



NvFlexHLibraryHolder::NvFlexHLibraryHolder() {
	++_instanceCount;
	if (nvFlexLibrary == NULL) {
		if (!cudaContextCreated) {
			if (!NvFlexDeviceCreateCudaContext(NvFlexDeviceGetSuggestedOrdinal()))throw std::runtime_error("Cannot initialize Cuda Context");
			cudaContextCreated = true;
		}
		NvFlexInitDesc desc;
		desc.deviceIndex = 0; // ignored, device index is set by the context
		desc.enableExtensions = false;
		desc.renderDevice = NULL;
		desc.renderContext = NULL;
		desc.computeType = NvFlexComputeType::eNvFlexCUDA;
		nvFlexLibrary = NvFlexInit(110, &nvFlexErrorCallbackPrint,&desc);
		std::cout << "flex library initialized" << std::endl;
	}
	std::cout << "libhld: instancecount: " << _instanceCount << std::endl;
}
NvFlexHLibraryHolder::~NvFlexHLibraryHolder() {
	--_instanceCount;
	std::cout << "libhld: instancecount: " << _instanceCount << std::endl;
	if (_instanceCount == 0) {
		//no, lets assume that we need to have proper context by this time for it to be properly released
		// and lets assume that NvFlexShutdown restores previous context
		while (releaseCudaContext()) {};//release all cuda contexts
		NvFlexAcquireContext(nvFlexLibrary);
		NvFlexShutdown(nvFlexLibrary);
		nvFlexLibrary = NULL;
		cudaContextAcquiredCount = 0;
		NvFlexDeviceDestroyCudaContext();
		cudaContextCreated = false;
		std::cout << "flex library destroyed" << std::endl;
	}
}



// debug helpers
static void CreateFluidParticleGrid(NvFlexExtParticleData& ptd, int* indices,  Vec3 lower, int dimx, int dimy, int dimz, float radius, Vec3 velocity, float invMass, int phase, float jitter)
{
	for (int x = 0; x < dimx; ++x)
	{
		for (int y = 0; y < dimy; ++y)
		{
			for (int z = 0; z < dimz; ++z)
			{
				
				Vec3 position = lower + Vec3(float(x), float(y), float(z))*radius + RandomUnitVector()*jitter;
				int ind = indices[z + dimz*(y + dimy*x)];
				ptd.particles[ind * 4 + 0] = position.x;
				ptd.particles[ind * 4 + 1] = position.y;
				ptd.particles[ind * 4 + 2] = position.z;
				ptd.particles[ind * 4 + 3] = invMass;

				ptd.velocities[ind * 3 + 0] = velocity.x;
				ptd.velocities[ind * 3 + 1] = velocity.y;
				ptd.velocities[ind * 3 + 2] = velocity.z;
				
				ptd.phases[ind] = phase;
			}
		}
	}
}