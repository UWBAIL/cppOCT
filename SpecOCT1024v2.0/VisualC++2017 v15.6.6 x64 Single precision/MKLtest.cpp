// octProc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <numeric> //for iota interation
#include <vector>
#include <fstream>
#include <stdio.h>

#include <algorithm> //provide function 'lower_bound' for interpolation
#include <complex>
#include <mkl.h>
#include <mkl_dfti.h>
#include <ctime>
#include <windows.h>

using namespace std;

const char* octfilename("data2OMAG.oct");
const char* refname("ref_data.txt");
//Function declaration
float polyval(vector<float>, int, float);
//interpolation
void dinterp1(float* data, int nrows, float* x, int N, float* result);
//void fft_complex(vector<complex<float>>& in,size_t NZ);



const float INF = 1.e100;//define inf for linear-interpolation check
DFTI_DESCRIPTOR_HANDLE descriptor;

int main() {

	ofstream outputfile("ED.bin", ios::binary);

	const long int nZ = 1024;
	const long int nX = 400;
	const long int nY = 400;
	const long int nR = 8;
	const long int nXR = nX * nR;
	size_t status; //check fread

	vector<float> coefs = { (float)11.366, (float)8.5808e-1, (float)2.4996e-4, (float)-1.1127e-7 };
	float KES[nZ];							//K-clock estimate
	vector<float> refdata(nZ);				//data reference
	vector<complex<float>> specC(nZ*nXR);	//Vector spectrum 
	vector<complex<float>> EDdata(nZ*nXR);	//dynamic data
	vector<float> buffer(nZ*nXR), tbuf(nZ*nXR);
	vector<unsigned short> buf(nZ*nXR);		//data buffer for binary read
	FILE* octfile = NULL;
	UINT32 binarytime = 0, castingtime = 0, interptime = 0,
		ffttime = 0, edtime = 0;

	//Linear interpolation parameter initialization
	//'interp1' equivalent to matlab (spline order 0 interp)
	MKL_INT bc_type = DF_NO_BC;         // boundary conditions type
										//MKL_INT ic_type;                    // internal conditions type
										//MKL_INT nscoeff;                    // number of spline coefficients
	MKL_INT scoeffhint = DF_NO_HINT;
	MKL_INT dorder[1] = { 1 };
	MKL_INT *cell_idx = 0;
	float* datahint = 0;
	/***** Parameters describing function *****/
	DFTaskPtr task;
	/* Limits of interpolation interval are provided in case
	of uniform partition */
	float K[2]; //Linear K-clock
	K[0] = 1; K[1] = nZ;

	/***** Parameters describing boundary conditions type *****/
	/* No boundary conditions are provided for linear spline */
	float *bc = 0;
	// pointer array of spline coefficients
	vector<float> scoeff (nX*nR*(nZ - 1)*DF_PP_LINEAR);
	float* rptr = new float[nZ*nX*nR]; //return pointer of interpolation results

	MKL_INT xhint = DF_UNIFORM_PARTITION;	//Uniform K-clock partition from K[0] to K[1]
	MKL_INT yhint = DF_MATRIX_STORAGE_ROWS; //data stored in rows, i.e. data(i+j*nZ);


											//=============FFT initialization===================//
	DftiCreateDescriptor(&descriptor, DFTI_SINGLE,
		DFTI_COMPLEX, 1, nZ); //Specify size and precision
							  //DftiSetValue(descriptor, DFTI_PLACEMENT, DFTI_NOT_INPLACE); //FFT output +input
	DftiSetValue(descriptor, DFTI_NUMBER_OF_TRANSFORMS, nXR);
	DftiSetValue(descriptor, DFTI_INPUT_DISTANCE, nZ);
	//DftiSetValue(descriptor, DFTI_OUTPUT_DISTANCE, nZ);
	DftiCommitDescriptor(descriptor); //Finalize the descriptor

									  /*===========================PCA initialization======================*/
	MKL_INT         m;				//Null value 
	MKL_INT ifail[nR], info;
	MKL_Complex8   alpha = { 1,0 }, beta = { 0,0 }, inverse = { -1,0 };
	MKL_Complex8  *matrixc = new MKL_Complex8[nR*nR];
	float w[nR];					//array of eigen values
	const long int RFilt = 3;
	MKL_Complex8 z[nR*nR] = { 0 }, ImEV[nR*nR] = { 0 }, complexone = { 1,0 };






	//========== MAIN PROCESS BEGIN===============/
	//polyval: create a grid of (physically) constant K-clock
	for (int i = 0; i < nZ; i++) {
		KES[i] = polyval(coefs, 4, i + 1);;
		//		cout << KES[i] << '\t';//Work correctly on 2017.07.13
	}

	clock_t begintime = clock();
	clock_t endtime = clock();

	//OPEN REF file
	ifstream ref, octbin;
	ref.open(refname);
	//check to see that the file was opened correctly:
	if (!ref.is_open()) {
		std::cerr << "There was a problem opening the ref-data file!\n";
		exit(1);//exit or do additional error checking
	}
	float a;

	//SAVE REFERENCE DATA TO ARRAY
	for (int i = 0; ref >> a; i++) {
		//		printf("%f ", a);
		refdata[i] = a;
	}
	ref.close();

	//OPEN OCT file
	//unsigned short* buf = new unsigned short[nZ*nXR];
	for (int iY = 0; iY < 111; iY++)
	{
		if (octfile == NULL) {
			if (fopen_s(&octfile, "data2OMAG.oct", "rb") == 0) {
				//fseek(octfile, nZ*nXR * iY, 0);
				//char is 1-byte, we want to read 2-byte ushort
				status = fread(buf.data(), sizeof(unsigned short), nZ*nXR, octfile);
				printf("Total number of element (usigned short) read: %zu\n", status);
				cout << "size of DATA: " << nZ << 'X' << nX
					<< 'X' << nR << 'X' << iY << '\n';
			}
			else {
				std::cerr << "There was a problem opening the oct file!\n";
				exit(2);//exit or do additional error checking
			}
		}
		else {
			//fseek(octfile, nZ*nXR * iY, 0);
			fread(buf.data(), sizeof(unsigned short), nZ*nXR, octfile);
			cout << "size of DATA: " << nZ << 'X' << nX << 'X'
				<< nR << 'X' << iY << '\n';
			if (feof(octfile)) {
				puts("End-of-File reached.");
				printf("Total number of bytes read: %zu\n", status);
			}
		}

		binarytime += clock() - endtime;
		cout << "Read binary time: " << float(binarytime) / CLOCKS_PER_SEC * 1000
			<< "ms \n";
		endtime = clock();

		/*-----Checked 2018.07.16 functioning correctly----------*/
		//Assigning buffer typedef of usignedshort* -> float*

		copy(buf.begin(), buf.end(), buffer.begin());
		for (int i = 0; i < nR; i++) {
			for (int j = 0; j < nX; j++) {
				cblas_saxpy(nZ, -1, refdata.data(), 1, &buffer[j*nZ + i * nZ*nX], 1);
			}
		}

		castingtime += clock() - endtime;
		cout << "Casting UINT to float time: " << float(castingtime) * 1000
			/ CLOCKS_PER_SEC << "ms \n";
		endtime = clock();



		/************** Create Data Fitting task ***********************/
		/***************************************************************/
		dfsNewTask1D(&task, nZ, K, xhint, nX*nR, buffer.data(), yhint);
		dfsEditPPSpline1D(task, DF_PP_LINEAR, DF_PP_DEFAULT, bc_type, 
			0, DF_NO_IC, 0,	scoeff.data(), scoeffhint);
		//***** Construct linear spline using STD method ****
		int error = dfsConstruct1D(task, DF_PP_SPLINE, DF_METHOD_STD);
		//Interpolate, careful with STORAGE_COLS and STORAGE_ROWS
		dfsInterpolate1D(task, DF_INTERP, DF_METHOD_PP,
			nZ, KES, DF_NON_UNIFORM_PARTITION, 1,
			dorder, datahint, tbuf.data(), DF_MATRIX_STORAGE_ROWS, cell_idx);
		//copy array of (float)rptr to specC complex<float> vector
		//copy(rptr, rptr + nZ * nX*nR, specC.begin());
		copy(tbuf.begin(), tbuf.end(), specC.begin());

		interptime += clock() - endtime;
		cout << "Interpolation time: " << float(interptime) / CLOCKS_PER_SEC * 1000
			<< "ms \n";
		endtime = clock();


		/*************************** FOURIER TRANSFORM  *****************/
		/***************************************************************/

		DftiComputeForward(descriptor, specC.data());

		ffttime += clock() - endtime;
		cout << "FFT time: " << float(ffttime) / CLOCKS_PER_SEC * 1000
			<< "ms \n";
		endtime = clock();



		/*--------------------EIGEN DECOMPOSITION: PCA METHOD--------------*/
		//Construct covariance matrix nRxnR
		cblas_cgemm(CblasColMajor, CblasConjTrans, CblasNoTrans, nR, nR, nX*nZ, &alpha,
			specC.data(), nX*nZ, specC.data(), nX*nZ, &beta, matrixc, nR);


		//Eigen value of first 'RFilt' eigen value, the eigen value is ascending
		info = LAPACKE_cheevx(LAPACK_COL_MAJOR, 'V', 'I', 'L', nR, matrixc, nR,
			0, 100000, nR - RFilt + 1, nR, -1, &m, w, z + (nR - RFilt)*nR, nR, ifail);
		for (int i = 0; i < nR; i++) {
			for (int j = 0; j < nR; j++) {
				ImEV[i*nR + j] = { 0,0 };
			}
			ImEV[i*nR + i] = { 1,0 };
		}

		cblas_cgemm(CblasColMajor, CblasNoTrans, CblasConjTrans, nR, nR, nR, &inverse,
			z, nR, z, nR, &alpha, ImEV, nR);

		//projection of filter eigenvector to original data

		cblas_cgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, nX*nZ, nR, nR, &alpha,
			specC.data(), nX*nZ, ImEV, nR, &beta, EDdata.data(), nX*nZ);

		/*cblas_chemm(CblasColMajor, CblasRight, CblasUpper, nX*nZ, nR, &alpha,
		ImEV, nR, specC.data(), nX*nZ, &beta, EDdata.data(), nX*nZ);*/


		edtime += clock() - endtime;
		cout << "ED time: " << float(edtime) / CLOCKS_PER_SEC * 1000
			<< "ms \n";
		endtime = clock();

		//cblas_zdotc_sub(EDdata.size(),EDdata.data(),1,EDdata.data(),1,specC.data());


	};
	endtime = clock();
	cout << "Total time: " << float(endtime - begintime) / CLOCKS_PER_SEC * 1000
		<< "ms \n";


	fclose(octfile);

	if (outputfile.is_open()) {
		for (size_t i = 0; i < nZ*nXR; i++) {
			outputfile.write((char*)(EDdata.data() + i), sizeof(complex<float>));
		}
	}


	getchar();


	return 0;
}




//POLYVAL function
float polyval(vector<float> p, int n, float x) {
	float px;
	px = 0;

	for (int k = 0; k < n; k++) {
		px += p[k] * pow(x, k);
	}

	return px;
}





void dinterp1(float* data, int nrows, float* x, int N, float* result) {
	for (int i = 0; i < N; i++) {
		// get coordinates of bounding grid locations
		long long x_1 = (long long)std::floor(x[i] - 1);

		// handle special case where x is the last element
		if ((x[i] - 1) == (nrows - 1)) { ; x_1 -= 1; }
		// return 0 for target values that are out of bounds
		if ((x_1 < 0) | (x_1 + 1) >(nrows - 1)) {
			result[i] = 0;
		}
		else {
			// get the array values
			const float& f_1 = data[x_1];
			const float& f_2 = data[x_1 + 1];
			// compute weights
			float w_x1 = x_1 + 1 - (x[i] - 1);
			result[i] = f_1 * w_x1 + f_2 - f_2 * w_x1;
		}
	}
}


