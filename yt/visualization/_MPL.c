/*******************************************************************************
# Copyright (c) 2013, yt Development Team.
#
# Distributed under the terms of the Modified BSD License.
#
# The full license is in the file COPYING.txt, distributed with this software.
*******************************************************************************/
//
// _MPL
//   A module for making static-resolution arrays representing
//   AMR data.
//

#include "Python.h"

#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <ctype.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include "numpy/ndarrayobject.h"

#define min(X,Y) ((X) < (Y) ? (X) : (Y))
#define max(X,Y) ((X) > (Y) ? (X) : (Y))

static PyObject *_pixelizeError;

char _pixelizeDocstring[] =
"Returns a static-resolution pixelized version of AMR data.\n\n"
"@parameter xp: ndarray of x centers\n"
"@Parameter yp: ndarray of y centers\n"
"@parameter dxp: ndarray of x half-widths\n"
"@parameter dyp: ndarray of y half-widths\n"
"@parameter dp: ndarray of data\n"
"@parameter rows: number of pixel rows\n"
"@parameter cols: number of pixel columns\n"
"@parameter bounds: (x_min, x_max, y_min, y_max)";

static PyObject* Py_Pixelize(PyObject *obj, PyObject *args) {

  PyObject *xp, *yp, *dxp, *dyp, *dp;
  PyArrayObject *x, *y, *dx, *dy, *d;
  unsigned int rows, cols;
  int antialias = 1;
  double x_min, x_max, y_min, y_max;
  double period_x, period_y;
  int check_period = 1, nx;
  int i, j, p, xi, yi;
  double lc, lr, rc, rr;
  double lypx, rypx, lxpx, rxpx, overlap1, overlap2;
  npy_float64 oxsp, oysp, xsp, ysp, dxsp, dysp, dsp;
  int xiter[2], yiter[2];
  double xiterv[2], yiterv[2];
  npy_intp dims[2];
  PyArrayObject *my_array;
  double width, height, px_dx, px_dy, ipx_dx, ipx_dy;
  PyObject *return_value;

  xp = yp = dxp = dyp = dp = NULL;
  x = y = dx = dy = d = NULL;

  period_x = period_y = 0;

  if (!PyArg_ParseTuple(args, "OOOOOII(dddd)|i(dd)i",
      &xp, &yp, &dxp, &dyp, &dp, &cols, &rows,
      &x_min, &x_max, &y_min, &y_max,
      &antialias, &period_x, &period_y, &check_period))
      return PyErr_Format(_pixelizeError, "Pixelize: Invalid Parameters.");

  width = x_max - x_min;
  height = y_max - y_min;
  px_dx = width / ((double) rows);
  px_dy = height / ((double) cols);
  ipx_dx = 1.0 / px_dx;
  ipx_dy = 1.0 / px_dy;

  // Check we have something to output to
  if (rows == 0 || cols ==0)
      PyErr_Format( _pixelizeError, "Cannot scale to zero size.");

  // Get numeric arrays
  x = (PyArrayObject *) PyArray_FromAny(xp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if (x == NULL) {
      PyErr_Format( _pixelizeError, "x is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  y = (PyArrayObject *) PyArray_FromAny(yp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((y == NULL) || (PyArray_SIZE(y) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "y is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  d = (PyArrayObject *) PyArray_FromAny(dp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((d == NULL) || (PyArray_SIZE(d) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "data is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  dx = (PyArrayObject *) PyArray_FromAny(dxp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((dx == NULL) || (PyArray_SIZE(dx) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "dx is of incorrect type (wanted 1D float)");
      goto _fail;
  }
  dy = (PyArrayObject *) PyArray_FromAny(dyp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((dy == NULL) || (PyArray_SIZE(dy) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "dy is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  // Check dimensions match
  nx = PyArray_DIMS(x)[0];

  // Calculate the pointer arrays to map input x to output x

  dims[0] = rows;
  dims[1] = cols;
  my_array =
    (PyArrayObject *) PyArray_SimpleNewFromDescr(2, dims,
              PyArray_DescrFromType(NPY_FLOAT64));
  //npy_float64 *gridded = (npy_float64 *) my_array->data;

  xiter[0] = yiter[0] = 0;
  xiterv[0] = yiterv[0] = 0.0;

  Py_BEGIN_ALLOW_THREADS
  for(i=0;i<rows;i++)for(j=0;j<cols;j++)
      *(npy_float64*) PyArray_GETPTR2(my_array, i, j) = 0.0;
  for(p=0;p<nx;p++)
  {
    // these are cell-centered
    oxsp = *((npy_float64 *)PyArray_GETPTR1(x, p));
    oysp = *((npy_float64 *)PyArray_GETPTR1(y, p));
    // half-width
    dxsp = *((npy_float64 *)PyArray_GETPTR1(dx, p));
    dysp = *((npy_float64 *)PyArray_GETPTR1(dy, p));
    dsp = *((npy_float64 *)PyArray_GETPTR1(d, p));
    xiter[1] = yiter[1] = 999;
    if(check_period == 1) {
      if (oxsp - dxsp < x_min) {xiter[1] = +1; xiterv[1] = period_x;}
      else if (oxsp + dxsp > x_max) {xiter[1] = -1; xiterv[1] = -period_x;}
      if (oysp - dysp < y_min) {yiter[1] = +1; yiterv[1] = period_y;}
      else if (oysp + dysp > y_max) {yiter[1] = -1; yiterv[1] = -period_y;}
    }
    overlap1 = overlap2 = 1.0;
    for(xi = 0; xi < 2; xi++) {
      if(xiter[xi] == 999)continue;
      xsp = oxsp + xiterv[xi];
      if((xsp+dxsp<x_min) || (xsp-dxsp>x_max)) continue;
      for(yi = 0; yi < 2; yi++) {
        if(yiter[yi] == 999)continue;
        ysp = oysp + yiterv[yi];
        if((ysp+dysp<y_min) || (ysp-dysp>y_max)) continue;
        lc = max(((xsp-dxsp-x_min)*ipx_dx),0);
        lr = max(((ysp-dysp-y_min)*ipx_dy),0);
        rc = min(((xsp+dxsp-x_min)*ipx_dx), rows);
        rr = min(((ysp+dysp-y_min)*ipx_dy), cols);
        for (i=lr;i<rr;i++) {
          lypx = px_dy * i + y_min;
          rypx = px_dy * (i+1) + y_min;
          if (antialias == 1) {
              overlap2 = ((min(rypx, ysp+dysp) - max(lypx, (ysp-dysp)))*ipx_dy);
          }
          if (overlap2 < 0.0) continue;
          for (j=lc;j<rc;j++) {
            lxpx = px_dx * j + x_min;
            rxpx = px_dx * (j+1) + x_min;
            if (antialias == 1) {
                overlap1 = ((min(rxpx, xsp+dxsp) - max(lxpx, (xsp-dxsp)))*ipx_dx);
            }
            if (overlap1 < 0.0) continue;
            if (antialias == 1)
              *(npy_float64*) PyArray_GETPTR2(my_array, j, i) +=
                    (dsp*overlap1)*overlap2;
            else *(npy_float64*) PyArray_GETPTR2(my_array, j, i) = dsp;
          }
        }
      }
    }
  }
  Py_END_ALLOW_THREADS

  // Attatch output buffer to output buffer

  Py_DECREF(x);
  Py_DECREF(y);
  Py_DECREF(d);
  Py_DECREF(dx);
  Py_DECREF(dy);

  return_value = Py_BuildValue("N", my_array);

  return return_value;

  _fail:

    if(x!=NULL)Py_XDECREF(x);
    if(y!=NULL)Py_XDECREF(y);
    if(d!=NULL)Py_XDECREF(d);
    if(dx!=NULL)Py_XDECREF(dx);
    if(dy!=NULL)Py_XDECREF(dy);
    return NULL;

}

static PyObject* Py_CPixelize(PyObject *obj, PyObject *args) {

  PyObject *xp, *yp, *zp, *pxp, *pyp,
           *dxp, *dyp, *dzp, *dp,
           *centerp, *inv_matp, *indicesp;
  PyArrayObject *x, *y, *z, *px, *py, *d,
                *dx, *dy, *dz, *center, *inv_mat, *indices;
  unsigned int rows, cols;
  double px_min, px_max, py_min, py_max;
  double width, height;
  long double px_dx, px_dy;
  int i, j, p, nx;
  int lc, lr, rc, rr;
  long double md, cxpx, cypx;
  long double cx, cy, cz;
  npy_float64 *centers;
  npy_intp *dims;

  PyArrayObject *my_array;
  npy_float64 *gridded;
  npy_float64 *mask;

  int pp;

  npy_float64 inv_mats[3][3];

  npy_float64 xsp;
  npy_float64 ysp;
  npy_float64 zsp;
  npy_float64 pxsp;
  npy_float64 pysp;
  npy_float64 dxsp;
  npy_float64 dysp;
  npy_float64 dzsp;
  npy_float64 dsp;

  PyObject *return_value;

  xp = yp = zp = pxp = pyp = dxp = dyp = dzp = dp = NULL;
  centerp = inv_matp = indicesp = NULL;

  x = y = z = px = py = dx = dy = dz = d = NULL;
  center = inv_mat = indices = NULL;

    if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOII(dddd)",
        &xp, &yp, &zp, &pxp, &pyp, &dxp, &dyp, &dzp, &centerp, &inv_matp,
        &indicesp, &dp, &cols, &rows, &px_min, &px_max, &py_min, &py_max))
        return PyErr_Format(_pixelizeError, "CPixelize: Invalid Parameters.");

  width = px_max - px_min;
  height = py_max - py_min;
  px_dx = width / ((double) rows);
  px_dy = height / ((double) cols);

  // Check we have something to output to
  if (rows == 0 || cols ==0)
      PyErr_Format( _pixelizeError, "Cannot scale to zero size.");

  // Get numeric arrays
  x = (PyArrayObject *) PyArray_FromAny(xp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if (x == NULL) {
      PyErr_Format( _pixelizeError, "x is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  y = (PyArrayObject *) PyArray_FromAny(yp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((y == NULL) || (PyArray_SIZE(y) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "y is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  z = (PyArrayObject *) PyArray_FromAny(zp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((z == NULL) || (PyArray_SIZE(y) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "z is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  px = (PyArrayObject *) PyArray_FromAny(pxp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((px == NULL) || (PyArray_SIZE(y) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "px is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  py = (PyArrayObject *) PyArray_FromAny(pyp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((py == NULL) || (PyArray_SIZE(y) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "py is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  d = (PyArrayObject *) PyArray_FromAny(dp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((d == NULL) || (PyArray_SIZE(d) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "data is of incorrect type (wanted 1D float)");
      goto _fail;
  }

  dx = (PyArrayObject *) PyArray_FromAny(dxp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((dx == NULL) || (PyArray_SIZE(dx) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "dx is of incorrect type (wanted 1D float)");
      goto _fail;
  }
  dy = (PyArrayObject *) PyArray_FromAny(dyp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((dy == NULL) || (PyArray_SIZE(dy) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "dy is of incorrect type (wanted 1D float)");
      goto _fail;
  }
  dz = (PyArrayObject *) PyArray_FromAny(dzp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, 0, NULL);
  if ((dz == NULL) || (PyArray_SIZE(dz) != PyArray_SIZE(x))) {
      PyErr_Format( _pixelizeError, "dz is of incorrect type (wanted 1D float)");
      goto _fail;
  }
  center = (PyArrayObject *) PyArray_FromAny(centerp,
            PyArray_DescrFromType(NPY_FLOAT64), 1, 1, NPY_ARRAY_C_CONTIGUOUS, NULL);
  if ((dz == NULL) || (PyArray_SIZE(center) != 3)) {
      PyErr_Format( _pixelizeError, "Center must have three points");
      goto _fail;
  }
  inv_mat = (PyArrayObject *) PyArray_FromAny(inv_matp,
            PyArray_DescrFromType(NPY_FLOAT64), 2, 2, 0, NULL);
  if ((inv_mat == NULL) || (PyArray_SIZE(inv_mat) != 9)) {
      PyErr_Format( _pixelizeError, "inv_mat must be three by three");
      goto _fail;
  }
  indices = (PyArrayObject *) PyArray_FromAny(indicesp,
            PyArray_DescrFromType(NPY_INT64), 1, 1, 0, NULL);
  if ((indices == NULL) || (PyArray_SIZE(indices) != PyArray_SIZE(dx))) {
      PyErr_Format( _pixelizeError, "indices must be same length as dx");
      goto _fail;
  }

  // Check dimensions match
  nx = PyArray_DIMS(x)[0];

  // Calculate the pointer arrays to map input x to output x

  centers = (npy_float64 *) PyArray_GETPTR1(center,0);

  dims[0] = rows;
  dims[1] = cols;
  my_array =
    (PyArrayObject *) PyArray_SimpleNewFromDescr(2, dims,
              PyArray_DescrFromType(NPY_FLOAT64));
  gridded = (npy_float64 *) PyArray_DATA(my_array);
  mask = malloc(sizeof(npy_float64)*rows*cols);

  for(i=0;i<3;i++)for(j=0;j<3;j++)
      inv_mats[i][j]=*(npy_float64*)PyArray_GETPTR2(inv_mat,i,j);

  for(p=0;p<cols*rows;p++)gridded[p]=mask[p]=0.0;
  for(pp=0; pp<nx; pp++)
  {
    p = *((npy_int64 *) PyArray_GETPTR1(indices, pp));
    xsp = *((npy_float64 *) PyArray_GETPTR1(x, p));
    ysp = *((npy_float64 *) PyArray_GETPTR1(y, p));
    zsp = *((npy_float64 *) PyArray_GETPTR1(z, p));
    pxsp = *((npy_float64 *) PyArray_GETPTR1(px, p));
    pysp = *((npy_float64 *) PyArray_GETPTR1(py, p));
    dxsp = *((npy_float64 *) PyArray_GETPTR1(dx, p));
    dysp = *((npy_float64 *) PyArray_GETPTR1(dy, p));
    dzsp = *((npy_float64 *) PyArray_GETPTR1(dz, p));
    dsp = *((npy_float64 *) PyArray_GETPTR1(d, p)); // We check this above
    // Any point we want to plot is at most this far from the center
    md = 2.0*sqrtl(dxsp*dxsp + dysp*dysp + dzsp*dzsp);
    if(((pxsp+md<px_min) ||
        (pxsp-md>px_max)) ||
       ((pysp+md<py_min) ||
        (pysp-md>py_max))) continue;
    lc = max(floorl((pxsp-md-px_min)/px_dx),0);
    lr = max(floorl((pysp-md-py_min)/px_dy),0);
    rc = min(ceill((pxsp+md-px_min)/px_dx),rows);
    rr = min(ceill((pysp+md-py_min)/px_dy),cols);
    for (i=lr;i<rr;i++) {
      cypx = px_dy * (i+0.5) + py_min;
      for (j=lc;j<rc;j++) {
        cxpx = px_dx * (j+0.5) + px_min;
        cx = inv_mats[0][0]*cxpx + inv_mats[0][1]*cypx + centers[0];
        cy = inv_mats[1][0]*cxpx + inv_mats[1][1]*cypx + centers[1];
        cz = inv_mats[2][0]*cxpx + inv_mats[2][1]*cypx + centers[2];
        if( (fabs(xsp-cx)*0.95>dxsp) || 
            (fabs(ysp-cy)*0.95>dysp) ||
            (fabs(zsp-cz)*0.95>dzsp)) continue;
        mask[j*cols+i] += 1;
        gridded[j*cols+i] += dsp;
      }
    }
  }
  for(p=0;p<cols*rows;p++)gridded[p]=gridded[p]/mask[p];

  // Attatch output buffer to output buffer

  Py_DECREF(x);
  Py_DECREF(y);
  Py_DECREF(z);
  Py_DECREF(px);
  Py_DECREF(py);
  Py_DECREF(d);
  Py_DECREF(dx);
  Py_DECREF(dy);
  Py_DECREF(dz);
  Py_DECREF(center);
  Py_DECREF(indices);
  Py_DECREF(inv_mat);
  free(mask);

  return_value = Py_BuildValue("N", my_array);

  return return_value;

  _fail:

    Py_XDECREF(x);
    Py_XDECREF(y);
    Py_XDECREF(z);
    Py_XDECREF(px);
    Py_XDECREF(py);
    Py_XDECREF(d);
    Py_XDECREF(dx);
    Py_XDECREF(dy);
    Py_XDECREF(dz);
    Py_XDECREF(center);
    Py_XDECREF(indices);
    Py_XDECREF(inv_mat);

    return NULL;

}

static PyMethodDef __MPLMethods[] = {
    {"Pixelize", Py_Pixelize, METH_VARARGS, _pixelizeDocstring},
    {"CPixelize", Py_CPixelize, METH_VARARGS, NULL},
    {NULL, NULL} /* Sentinel */
};

/* platform independent*/
#ifdef MS_WIN32
__declspec(dllexport)
#endif


PyMODINIT_FUNC
#if PY_MAJOR_VERSION >= 3
#define _RETVAL m
PyInit__MPL(void)
#else
#define _RETVAL 
init_MPL(void)
#endif
{
    PyObject *m, *d;
#if PY_MAJOR_VERSION >= 3
    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_MPL",           /* m_name */
        "Pixelization routines\n",
                             /* m_doc */
        -1,                  /* m_size */
        __MPLMethods,    /* m_methods */
        NULL,                /* m_reload */
        NULL,                /* m_traverse */
        NULL,                /* m_clear */
        NULL,                /* m_free */
    };
    m = PyModule_Create(&moduledef); 
#else
    m = Py_InitModule("_MPL", __MPLMethods);
#endif
    d = PyModule_GetDict(m);
    _pixelizeError = PyErr_NewException("_MPL.error", NULL, NULL);
    PyDict_SetItemString(d, "error", _pixelizeError);
    import_array();
    return _RETVAL;
}