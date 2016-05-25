#include "network.h"

#include <stdio.h>

using namespace std;

CvNetwork::CvNetwork():m_solver(0),m_cnn(0)
{
}

CvNetwork::~CvNetwork(void)
{
  if ( m_solver ){ delete m_solver; m_solver=0; }
  if ( m_cnn ) { m_cnn->release( (CvCNNStatModel**)&m_cnn ); m_cnn=0; }
}

void CvNetwork::loadModel(string inFile)
{
  CV_FUNCNAME("CvNetwork::loadModel");
  __BEGIN__;
  CvCNNLayer * layer = 0;
  CvFileStorage * fs = cvOpenFileStorage(inFile.c_str(),0,CV_STORAGE_READ);
  CvFileNode * root = cvGetRootFileNode( fs );
  CvFileNode * node = 0;
  char nodename[10]={0,};
  float lr_init = m_solver->lr_init();
  int decay_type = m_solver->decay_type();

  node = cvGetFileNodeByName(fs,root,"data");
  int n_input_planes = 1;
  int input_height   = 1;
  int input_width    = 1;
  int n_output_planes = 1;
  int output_height   = 1;
  int output_width    = 1;
  
  int dtype = CV_32F;
  const char * dtypename = cvReadStringByName(fs,node,"dtype","float");
  if (!strcmp(dtypename,"float")){dtype=CV_32F;
  }else if (!strcmp(dtypename,"double")){dtype=CV_64F;
  }else{fprintf(stderr,"Error: unknown dtype name `%s`\n",dtypename);exit(-1);}
  
  m_cnn = (CvCNNStatModel*)cvCreateCNNStatModel(
    CV_STAT_MODEL_MAGIC_VAL|CV_CNN_MAGIC_VAL, sizeof(CvCNNStatModel));

  CvFileNode * layers = cvGetFileNodeByName(fs,0,"layers");
  assert(CV_NODE_IS_SEQ(layers->tag));
  CvSeq * seq = layers->data.seq;
  int ii, total = seq->total;
  CvSeqReader reader;
  cvStartReadSeq( seq, &reader, 0 );
  
  CvStringHashNode * layer_key = cvGetHashedKey( fs, "name", -1, 1 );
  for (ii=0;ii<total;ii++){
    node = (CvFileNode*)reader.ptr;
    if (!node){break;}
    const char * predefined = cvReadStringByName(fs,node,"predefined","");
    const char * type = cvReadStringByName(fs,node,"type","");
    const char * name = cvReadStringByName(fs,node,"name","");
    const int visualize = cvReadIntByName(fs,node,"visualize",0);
    const char * activation_type = cvReadStringByName(fs,node,"activation_type","none");

    // parse layer-specific parameters
    if (strlen(predefined)>0){
      CvCNNLayer * predefined_layer = m_cnn->network->get_layer(m_cnn->network,predefined);
      if (icvIsCNNRecurrentNNLayer(predefined_layer)){
        int time_index = cvReadIntByName(fs,node,"time_index",0);
        CvCNNRecurrentLayer * recurrent_layer = (CvCNNRecurrentLayer*)predefined_layer;
        layer = cvCreateCNNRecurrentLayer( 
          predefined_layer->dtype, predefined_layer->name, predefined_layer, 
          predefined_layer->n_input_planes, predefined_layer->n_output_planes, 
          recurrent_layer->n_hiddens, recurrent_layer->seq_length, time_index, lr_init, decay_type, 
          recurrent_layer->activation_type, NULL, NULL, NULL );
        if (((CvCNNRecurrentLayer*)layer)->Wxh){
          cvReleaseMat(&((CvCNNRecurrentLayer*)layer)->Wxh);((CvCNNRecurrentLayer*)layer)->Wxh=0;}
        if (((CvCNNRecurrentLayer*)layer)->Whh){
          cvReleaseMat(&((CvCNNRecurrentLayer*)layer)->Whh);((CvCNNRecurrentLayer*)layer)->Whh=0;}
        if (((CvCNNRecurrentLayer*)layer)->Why){
          cvReleaseMat(&((CvCNNRecurrentLayer*)layer)->Why);((CvCNNRecurrentLayer*)layer)->Why=0;}
      }else if (icvIsCNNImgCroppingLayer(predefined_layer)){
        int time_index = cvReadIntByName(fs,node,"time_index",0);
        CvCNNImgCroppingLayer * this_layer = (CvCNNImgCroppingLayer*)predefined_layer;
        CvCNNLayer * input_layer = (this_layer->input_layers.size()>0?this_layer->input_layers[0]:0);
        layer = cvCreateCNNImgCroppingLayer( 
          this_layer->dtype, this_layer->name, this_layer->visualize, input_layer, 
          this_layer->n_output_planes, this_layer->output_height, this_layer->output_width, 
          this_layer->seq_length, time_index, this_layer->init_learn_rate, this_layer->decay_type );
      }else if (icvIsCNNFullConnectLayer(predefined_layer)){
        CvCNNFullConnectLayer * this_layer = (CvCNNFullConnectLayer*)predefined_layer;
        layer = cvCreateCNNFullConnectLayer( 
          this_layer->dtype, this_layer->name, this_layer->visualize, 
          this_layer->input_layers.size()>0?this_layer->input_layers[0]:0, 
          this_layer->n_input_planes, this_layer->n_output_planes, 
          this_layer->init_learn_rate, this_layer->decay_type, this_layer->activation_type, NULL );
      }else if (icvIsCNNConvolutionLayer(predefined_layer)){
        CvCNNConvolutionLayer * this_layer = (CvCNNConvolutionLayer*)predefined_layer;
        layer = cvCreateCNNConvolutionLayer( 
          this_layer->dtype, this_layer->name, predefined_layer, this_layer->visualize, 
          this_layer->input_layers.size()>0?this_layer->input_layers[0]:0, 
          this_layer->n_input_planes, this_layer->input_height, this_layer->input_width,
          this_layer->n_output_planes, this_layer->K,
          this_layer->init_learn_rate, this_layer->decay_type, this_layer->activation_type, NULL, NULL );
      }else if (icvIsCNNSubSamplingLayer(predefined_layer)){
        CvCNNSubSamplingLayer * this_layer = (CvCNNSubSamplingLayer*)predefined_layer;
        layer = cvCreateCNNSubSamplingLayer( 
          this_layer->dtype, this_layer->name, this_layer->visualize,
          this_layer->n_input_planes, this_layer->input_height, this_layer->input_width,
          this_layer->sub_samp_scale, this_layer->init_learn_rate, this_layer->decay_type, 0 );
      }else{
        assert(false);
      }
      // release weights matrix, use the one on predefined layer instead.
      if (layer->weights){cvReleaseMat(&layer->weights);layer->weights=0;}
      n_input_planes = layer->n_output_planes; 
      input_height = layer->output_height; 
      input_width = layer->output_width;
    }else if (!strcmp(type,"Convolution")){ // convolution layer
      CvCNNLayer * input_layer = 0; 
      const char * input_layer_name = cvReadStringByName(fs,node,"input_layer","");
      if (strlen(input_layer_name)>0){
        input_layer = m_cnn->network->get_layer(m_cnn->network,input_layer_name);
      }
      n_output_planes = cvReadIntByName(fs,node,"n_output_planes");
      int ksize = cvReadIntByName(fs,node,"ksize");
      layer = cvCreateCNNConvolutionLayer( dtype, name, 0, visualize, input_layer, 
        n_input_planes, input_height, input_width, n_output_planes, ksize,
        lr_init, decay_type, activation_type, NULL, NULL );
      if (input_layer){input_layer->output_layers.push_back(layer);}
      n_input_planes = n_output_planes;
      input_height = input_height-ksize+1;
      input_width = input_width-ksize+1;
    }else if (!strcmp(type,"SubSampling")){ // max pooling layer
      CvCNNLayer * input_layer = 0; 
      const char * input_layer_name = cvReadStringByName(fs,node,"input_layer","");
      if (strlen(input_layer_name)>0){
        input_layer = m_cnn->network->get_layer(m_cnn->network,input_layer_name);
      }
      int ksize = cvReadIntByName(fs,node,"ksize");
      layer = cvCreateCNNSubSamplingLayer( dtype, name, visualize,
        n_input_planes, input_height, input_width, ksize,
        lr_init, decay_type, NULL);
      if (input_layer){input_layer->output_layers.push_back(layer);}
      n_input_planes = n_output_planes;
      input_height = input_height/ksize;
      input_width = input_width/ksize;
    }else if (!strcmp(type,"FullConnect")){ // full connection layer
      CvCNNLayer * input_layer = 0; 
      const char * input_layer_name = cvReadStringByName(fs,node,"input_layer","");
      if (strlen(input_layer_name)>0){
        input_layer = m_cnn->network->get_layer(m_cnn->network,input_layer_name);
        if (!input_layer){LOGE("input_layer [%s] not found.",input_layer_name);exit(-1);}
        n_input_planes = input_layer->n_output_planes*input_layer->output_height*input_layer->output_width;
      }else{
        n_input_planes = n_input_planes*input_height*input_width;
      }
      n_output_planes = cvReadIntByName(fs,node,"n_output_planes");
      layer = cvCreateCNNFullConnectLayer( dtype, name, visualize, input_layer, 
        n_input_planes, n_output_planes, 
        lr_init, decay_type, activation_type, NULL );
      if (input_layer){input_layer->output_layers.push_back(layer);}
      n_input_planes = n_output_planes; input_height = 1; input_width = 1;
    }else if (!strcmp(type,"ImgCropping")){ // image cropping layer
      const char * input_layer_name = cvReadStringByName(fs,node,"input_layer","");
      if (strlen(input_layer_name)<1){
        LOGE("input layer name is required while defining ImgCropping layer."); exit(-1);}
      CvCNNLayer * input_layer = m_cnn->network->get_layer(m_cnn->network,input_layer_name);
      if (!input_layer){
        LOGE("input layer is not found while defining ImgCropping layer."); exit(-1);}
      n_output_planes = cvReadIntByName(fs,node,"n_output_planes",1);
      output_height = cvReadIntByName(fs,node,"output_height",output_height);
      output_width = cvReadIntByName(fs,node,"output_width",output_width);
      const int seq_length = cvReadIntByName(fs,node,"seq_length",1);
      const int time_index = cvReadIntByName(fs,node,"time_index",0);
      layer = cvCreateCNNImgCroppingLayer( dtype, name, visualize, input_layer, 
        n_output_planes, output_height, output_width, seq_length, time_index, 
        lr_init, decay_type );
      n_input_planes = n_output_planes; // input_height = 1; input_width = 1;
      input_height = output_height; input_width = output_width;
    }else if (!strcmp(type,"RecurrentNN")){ // recurrent layer
      const int n_input_planes_default = n_input_planes * input_height * input_width;
      n_input_planes = cvReadIntByName(fs,node,"n_input_planes",n_input_planes_default);
      n_output_planes = cvReadIntByName(fs,node,"n_output_planes",1);
      const int n_hiddens_default = cvCeil(exp2((log2(n_input_planes)+log2(n_output_planes))*.5f));
      const int n_hiddens = cvReadIntByName(fs,node,"n_hiddens",n_hiddens_default);
      const int seq_length = cvReadIntByName(fs,node,"seq_length",1);
      const int time_index = cvReadIntByName(fs,node,"time_index",0);
      layer = cvCreateCNNRecurrentLayer( dtype, name, 0, 
        n_input_planes, n_output_planes, n_hiddens, seq_length, time_index, 
        lr_init, decay_type, activation_type, NULL, NULL, NULL );
      n_input_planes = n_output_planes; input_height = 1; input_width = 1;
    }else if (!strcmp(type,"InputData")){ // data container layer
      n_input_planes = cvReadIntByName(fs,node,"n_input_planes",1);
      input_height   = cvReadIntByName(fs,node,"input_height",1);
      input_width    = cvReadIntByName(fs,node,"input_width",1);
      const int seq_length = cvReadIntByName(fs,node,"seq_length",1);
      layer = cvCreateCNNInputDataLayer( dtype, name, 
        n_input_planes, input_height, input_width, seq_length,
        lr_init, decay_type );
    }else if (!strcmp(type,"MultiTarget")){ // merge multiple data source  layer
      int n_input_layers = 1;
      const char * input_layer_names = cvReadStringByName(fs,node,"input_layers","");
      n_output_planes = cvReadIntByName(fs,node,"n_output_planes",1);
      CvCNNLayer ** input_layers = new CvCNNLayer*[256];
      char * input_layer_name = strtok((char*)input_layer_names," ,");
      input_layers[0] = m_cnn->network->get_layer(m_cnn->network, input_layer_name);
      int output_plane_count = input_layers[0]->n_output_planes;
      for (int ii=1; ii<256; ii++){
        input_layer_name = strtok(0," ,");
        if (!input_layer_name){break;}else{n_input_layers++;}
        input_layers[ii] = m_cnn->network->get_layer(m_cnn->network, input_layer_name);
        output_plane_count += input_layers[ii]->n_output_planes;
      }
      if (output_plane_count!=n_output_planes){
        CV_ERROR(CV_StsBadArg,"Invalid definition of `input_layers` in MultiTarget layer.");
      }
      layer = cvCreateCNNMultiTargetLayer( dtype, name, visualize, 
        n_input_layers, input_layers, n_output_planes, lr_init, decay_type );
      for (int lidx=0;lidx<n_input_layers;lidx++){input_layers[lidx]->output_layers.push_back(layer);}
      if (input_layers){delete [] input_layers; input_layers = 0;}
    }else{
      fprintf(stderr,"ERROR: unknown layer type %s\n",type);
    }

    // add layer to network
    if (!m_cnn->network){m_cnn->network = cvCreateCNNetwork(layer); 
    }else{m_cnn->network->add_layer( m_cnn->network, layer );}

    CV_NEXT_SEQ_ELEM( seq->elem_size, reader );
  }

  if (fs){cvReleaseFileStorage(&fs);fs=0;}
  __END__;
}

void CvNetwork::loadWeights(string inFile)
{
  if (m_cnn == NULL){fprintf(stderr,"ERROR: CNN has not been built yet\n");exit(0);}
  CvFileStorage * fs = cvOpenFileStorage(inFile.c_str(),0,CV_STORAGE_READ);
  m_cnn->network->read(m_cnn->network,fs);
  cvReleaseFileStorage(&fs);
}

void CvNetwork::saveWeights(string outFile)
{
  if(m_cnn == NULL){fprintf(stderr,"ERROR: CNN has not been built yet\n");exit(0);}
  CvFileStorage * fs = cvOpenFileStorage(outFile.c_str(),0,CV_STORAGE_WRITE);
  m_cnn->network->write(m_cnn->network,fs);
  cvReleaseFileStorage(&fs);
}

void CvNetwork::train(CvMat *trainingData, CvMat *responseMat)
{
  int i, j;	
  CvCNNStatModelParams params;
  assert(CV_MAT_TYPE(trainingData->type)==CV_32F);

  CvCNNLayer * last_layer = cvGetCNNLastLayer(m_cnn->network);
  int n_outputs = last_layer->n_output_planes;
  if (icvIsCNNRecurrentNNLayer(last_layer)){
    n_outputs *= ((CvCNNRecurrentLayer*)last_layer)->seq_length;
  }

  params.cls_labels = cvCreateMat( n_outputs, n_outputs, CV_32FC1 );
  cvSet(params.cls_labels,cvScalar(1));
  params.etalons = cvCreateMat( n_outputs, n_outputs, CV_32FC1 );
  cvSetIdentity(params.etalons,cvScalar(2));
  cvSubS(params.etalons,cvScalar(1),params.etalons);

  params.network = m_cnn->network;
  params.start_iter=0;
  params.max_iter=m_solver->maxiter();
  params.batch_size = m_solver->batch_size();
  params.grad_estim_type=CV_CNN_GRAD_ESTIM_RANDOM;

  if (CV_MAT_TYPE(responseMat->type)!=CV_32S){
    CvMat * tmp = cvCreateMat(responseMat->rows,responseMat->cols,CV_32S);
    cvConvert(responseMat,tmp);
    m_cnn = cvTrainCNNClassifier( trainingData, CV_ROW_SAMPLE,tmp,&params,0,0,0,0);
    cvReleaseMat(&tmp);
  }else{
    m_cnn = cvTrainCNNClassifier( trainingData, CV_ROW_SAMPLE,responseMat,&params,0,0,0,0);
  }
}

float CvNetwork::evaluate(CvMat * testing, CvMat * expected, int nsamples)
{
  CvMat samples = cvMat(nsamples, testing->cols, CV_32F, testing->data.fl+nsamples*testing->cols);
  CvMat * result = cvCreateMat(expected->cols, nsamples, CV_32F);
  CvCNNLayer * last_layer = m_cnn->network->get_last_layer(m_cnn->network);

  // testing data
  cvGetRows(testing,&samples,0,nsamples);
  m_cnn->predict(m_cnn,&samples,result);

  // 4) compute loss & accuracy, print progress
  CvMat * expected_transpose = cvCreateMat(expected->cols,nsamples,CV_32F);
  CvMat expected_submat_hdr;
  cvGetRows(expected,&expected_submat_hdr,0,nsamples);
  cvTranspose(&expected_submat_hdr,expected_transpose);
  float trloss = cvNorm(result, expected_transpose)/float(nsamples);
  float top1 = m_cnn->network->eval(last_layer, result, expected_transpose);
  static double sumloss = trloss;
  static double sumacc  = top1;
  {
    fprintf(stderr, "sumacc: %.1f%%[%.1f%%], sumloss: %f\n", sumacc,top1,sumloss);
    CvMat * Xn_transpose = cvCreateMat(nsamples,last_layer->n_output_planes,CV_32F);
    cvTranspose(result,Xn_transpose);
    // {fprintf(stderr,"output:\n");cvPrintf(stderr,"%.1f ", Xn_transpose);}
    // {fprintf(stderr,"expect:\n");cvPrintf(stderr,"%.1f ", &expected_submat_hdr);}
    cvReleaseMat(&Xn_transpose);
  }

  cvReleaseMat(&result);
  cvReleaseMat(&expected_transpose);
  return top1;
}



