#include <rtxi-lfpInferenceEngine.h>

using namespace std;

std::vector<int> PyList_toVecInt(PyObject* py_list);

extern "C" Plugin::Object *createRTXIPlugin(void){
    return new rtxilfpInferenceEngine();
}

static DefaultGUIModel::variable_t vars[] = {

  // Parameters
  { "Time Window (s)", "Time Window (s)", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE},
  { "Sampling Rate (Hz)", "Sampling Rate (Hz)", DefaultGUIModel::STATE | DefaultGUIModel::DOUBLE,},
  //{ "LF Lower Bound", "LF Lower Bound", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE,},
  //{ "LF Upper Bound", "LF Upper Bound", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE,},
  //{ "HF Lower Bound", "HF Lower Bound", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE,},
  //{ "HF Upper Bound", "HF Upper Bound", DefaultGUIModel::PARAMETER | DefaultGUIModel::DOUBLE,},
  { "Animal", "Animal", DefaultGUIModel::COMMENT, },  
  { "Model", "Model", DefaultGUIModel::COMMENT, },  

  // Inputs
  { "input_LFP", "Input LFP", DefaultGUIModel::INPUT | DefaultGUIModel::DOUBLE,},

  //Outputs
  { "ratio", "Output LFP Power Ratio", DefaultGUIModel::OUTPUT | DefaultGUIModel::DOUBLE,},
  { "LF Power", "Power in LF Band", DefaultGUIModel::OUTPUT | DefaultGUIModel::DOUBLE,},
  { "HF Power", "Power in HF Band", DefaultGUIModel::OUTPUT | DefaultGUIModel::DOUBLE,},
  { "State", "Estimated State", DefaultGUIModel::OUTPUT | DefaultGUIModel::UINTEGER,}
};

static size_t num_vars = sizeof(vars) / sizeof(DefaultGUIModel::variable_t);

// defining what's in the object's constructor
// sampling set by RT period
rtxilfpInferenceEngine::rtxilfpInferenceEngine(void) : DefaultGUIModel("lfpInferenceEngine with Custom GUI", ::vars, ::num_vars),
period(((double)RT::System::getInstance()->getPeriod())*1e-9), // grabbing RT period
sampling(1.0/period), // calculating RT sampling rate
lfpratiometer(N, sampling), // constructing lfpRatiometer object
lfpinferenceengine()
DEFAULT_ANIMAL("no-animal")
DEFAULT_MODEL("no-model")
{
    setWhatsThis("<p><b>lfpInferenceEngine:</b><br>Given an lfp input, this module estimates the cortical state.</p>");
    
    animal = DEFAULT_ANIMAL;
    model = DEFAULT_MODEL;

    DefaultGUIModel::createGUI(vars, num_vars);
    customizeGUI();
    update(INIT);
    refresh();
    QTimer::singleShot(0, this, SLOT(resizeMe()));
    
}

// defining what's in the object's destructor
rtxilfpRatiometer::~rtxilfpRatiometer(void) { }

// real-time RTXI function
void rtxilfpRatiometer::execute(void) {

  // push new time series reading to lfpRatiometer
  lfpratiometer.pushTimeSample(input(0));

  // calculate LF/HF ratio
  lfpratiometer.calcRatio();

  // estimate cortical state from FFT data
  lfpratiometer.makeFFT();
  
  lfpinferenceengine.arguments_predict = {"pyfuncs","predict"};
  lfpinferenceengine.PyArgs = {lfpinferenceengine.getModel(),
                                lfpinferenceengine.getFeats(),
                                lfpinferenceengine.getScaler(),
                                lfpinferenceengine.getData()};
  lfpinferenceengine.callPythonFunction(arguments_predict, pyArgs);

  lfpinferenceengine.state_vec = PyList_toVecInt(lfpinferenceengine.getResult());


  // put the LF/HF ratio into the output
  output(0) = lfpratiometer.getRatio();
  output(1) = lfpratiometer.getLFpower();
  output(2) = lfpratiometer.getHFpower();
  if (!state_vec.empty()) {
    lfpinferenceengine.state = vec.back().c();
  }else{
    lfpinferenceengine.state = -1;
  }
  output(4) = lfpinferenceengine.state;
    
}

// update function (not running in real time)
void rtxilfpRatiometer::update(DefaultGUIModel::update_flags_t flag)
{
  switch (flag) {
    case INIT:
      setParameter("Time Window (s)", sampling/N);
      setState("Sampling Rate (Hz)", sampling);
      // get bounds from lfpratiometer object
      setParameter("LF Lower Bound", lfpratiometer.getFreqBounds()[0]);
      setParameter("LF Upper Bound", lfpratiometer.getFreqBounds()[1]);
      setParameter("HF Lower Bound", lfpratiometer.getFreqBounds()[2]);
      setParameter("HF Upper Bound", lfpratiometer.getFreqBounds()[3]);

      setParameter("Animal",animal);
      setParameter("Model",model);

      break;

    case MODIFY:
      // defining parameters needed for constructor
      period = ((double)RT::System::getInstance()->getPeriod())*1e-9;
      sampling = 1.0/period;
      setState("Sampling Rate (Hz)", sampling); // updating GUI
      N = (int) (getParameter("Time Window (s)").toDouble() * sampling);

      // making new FFT plan
      lfpratiometer.changeFFTPlan(N, sampling);

      // setting frequency bounds based on user input
      lfpratiometer.setRatioParams(getParameter("LF Lower Bound").toDouble(),
          getParameter("LF Upper Bound").toDouble(),
          getParameter("HF Lower Bound").toDouble(),
          getParameter("HF Upper Bound").toDouble());

      lfpinferenceengine.init(getParameter("Animal"),getParameter("Model"));
      
      // setting DFT windowing function choice
      if (windowShape->currentIndex() == 0) {
        lfpratiometer.window_rect();
      }
      else if (windowShape->currentIndex() == 1) {
        lfpratiometer.window_hamming();
      }

      // clearing time series
      lfpratiometer.clrTimeSeries();

      break;

    case UNPAUSE:
      break;

    case PAUSE:
      lfpratiometer.clrTimeSeries();
      break;

    case PERIOD:
      break;

    default:
      break;
  }
}

// RTXI's customizeGUI function
void rtxilfpRatiometer::customizeGUI(void)
{
  QGridLayout* customlayout = DefaultGUIModel::getLayout();

  // adding dropdown menu for choosing FFT window shape
  windowShape = new QComboBox;
  windowShape->insertItem(1, "Rectangular");
  windowShape->insertItem(2, "Hamming");

  customlayout->addWidget(windowShape, 2, 0);
  setLayout(customlayout);
}

std::vector<int> PyList_toVecInt(PyObject* py_list) {
  if (PySequence_Check(py_list)) {
    PyObject* seq = PySequenceFast(py_list, "expected a sequence");
    if (seq != NULL){
      std::vector<int> my_vector;
      my_vectoor.reserve(PySequence_Fast_GET_SIZE(seq));
      for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(seq); i++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq,i);
        if(PyNNumber_Check(item)){
          Py_ssize_t value = PyNumber_AsSsize_t(item, PyExc_OverflowError);
          if (value == -1 && PyErr_Occurred()) {
            //handle error
          }
          my_vector.push_back(value);
        } else {
          //handle error
        }
      }
      Py_DECREF(seq);
      return my_vector;
    } else {
      //handle error
    }
  }else{
    //handle error
  }
}