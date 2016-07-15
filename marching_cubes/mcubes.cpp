
#include "mcubes.h"

//methods for doing feature detection and sample point computation

//sets feature value in gridcell
//0=no feature, return 1=edge, 2=corner
void isFeature(GRIDCELL &cell, const vector<Eigen::Vector3f> &normals, float feat_thresh, float corner_thresh){
    //check for feature in cube
    //get opening angles of normals
    Eigen::Vector3f norm_0;
    Eigen::Vector3f norm_1;
    float min=5.0;
    for(int n=0; n<7; n++){
        if(cell.normal_indexes[n]==-1) continue;
        for(int m=n+1; m<8; m++){
            if(cell.normal_indexes[m]==-1) continue;
            Eigen::Vector3f n1 = normals[cell.normal_indexes[n]];
            Eigen::Vector3f n2 = normals[cell.normal_indexes[m]];
            float angle = n1[0]*n2[0]+n1[1]*n2[1]+n1[2]*n2[2];
            if(angle<min){
                min=angle;
                norm_0=n1;
                norm_1=n2;
            }
        }
    }
    //check if within threshold for feature detection
    if(min>=feat_thresh){ //is not a feature
        cell.feature=0;
        return;
    }

    //check if feature is a corner
    Eigen::Vector3f n_star;
    n_star[0] = norm_0[1]*norm_1[2]-norm_0[2]*norm_1[1];
    n_star[1] = norm_0[2]*norm_1[0]-norm_0[0]*norm_1[2];
    n_star[2] = norm_0[0]*norm_1[1]-norm_0[1]*norm_1[0];

    float max_ang=0.0;
    for(int n=0; n<8; n++){
        if(cell.normal_indexes[n]==-1) continue;
        Eigen::Vector3f norm = normals[cell.normal_indexes[n]];
        float corner_ang=norm[0]*n_star[0]+norm[1]*n_star[1]+norm[2]*n_star[2];
        corner_ang = fabs(corner_ang);
        if(corner_ang>max_ang)max_ang=corner_ang;
    }
    if(max_ang>corner_thresh){ //is a corner
        cell.feature=2;
        return;
    }
    cell.feature=1; //is an edge
}

//compute linear shift to center the grid
Eigen::Vector3f getTransform(const GRIDCELL &cell){
    //get centroid
    Eigen::Vector3f shift;
    shift[0]=shift[1]=shift[2]=0.0;
    for(int i=0; i<8; i++){
        shift[0]+=cell.p[i].x;
        shift[1]+=cell.p[i].y;
        shift[2]+=cell.p[i].z;
    }
    shift[0]/=8.0;
    shift[1]/=8.0;
    shift[2]/=8.0;
    return shift;
}

Eigen::MatrixXf getNormMat(const GRIDCELL &cell, const vector<Eigen::Vector3f> &normals){
    Eigen::MatrixXf m(8,3);
    int size=0;
    for(int n=0; n<8; n++){
        if(cell.normal_indexes[n]==-1) continue;
        Eigen::Vector3f norm = normals[cell.normal_indexes[n]];
        m(size,0)=norm[0];m(size,1)=norm[1];m(size,2)=norm[2];
        size++;
    }
    m.conservativeResize(size, 3);
    return m;
}

Eigen::VectorXf getVec(const GRIDCELL &cell, const vector<Eigen::Vector3f> &normals, const Eigen::Vector3f &shift){
    Eigen::VectorXf vec(8);
    int size=0;
    for(int n=0; n<8; n++){
        if(cell.normal_indexes[n]==-1) continue;
        Eigen::Vector3f s;
        s[0]=(float)cell.p[n].x-shift[0];s[1]=(float)cell.p[n].y-shift[1];s[2]=(float)cell.p[n].z-shift[2];
        Eigen::Vector3f norm = normals[cell.normal_indexes[n]];
        vec[size]=s[0]*norm[0]+s[1]*norm[1]+s[2]*norm[2];
        size++;
    }
    vec.conservativeResize(size);
    return vec;
}

//compute new sample point for feature
Eigen::Vector3f computePoint(const Eigen::MatrixXf &normalMat, const Eigen::VectorXf &vec, const Eigen::Vector3f &shift, const bool isEdge){
    Eigen::JacobiSVD<Eigen::MatrixXf> svd(normalMat, Eigen::ComputeThinU | Eigen::ComputeThinV);
    float max = svd.singularValues()(0);
    float min = svd.singularValues()(svd.singularValues().rows()-1);
    float thresh = min/(max-0.0001);
    if(isEdge) svd.setThreshold(thresh);
    Eigen::Vector3f point = svd.solve(vec);
    point[0]+=shift[0];point[1]+=shift[1];point[2]+=shift[2];
    return point;
}

//*********************************************************************************************
//methods for extended marching cubes

vector<GRIDCELL> getGridCells(gridPtr in, gridPtr surfaceMap, const vector<Eigen::Vector3f> &normals, float feat_thresh, float corner_thresh, const bool USING_FEATURES){
    vector<GRIDCELL> cells;
    for(int i=0; i<in->dims[0]-1; i++){
        for(int j=0; j<in->dims[1]-1; j++){
            for(int k=0; k<in->dims[2]-1; k++){
                GRIDCELL cell;
                //fill gridcell with values
                for(int n=0; n<8; n++){
                    XYZ pnt;
                    int x = (i+(int)((n%4==1)||(n%4)==2));
                    int y = (j+((n%4)/2));
                    int z = (k+(n/4));
                    pnt.x=(float)x;
                    pnt.y=(float)y;
                    pnt.z=(float)z;
                    cell.p[n]=pnt;
                    cell.val[n]=(*in)[x][y][z];
                    cell.normal_indexes[n]=(int)(*surfaceMap)[x][y][z];
                }

                if(USING_FEATURES){
                    //set cell feature value
                    isFeature(cell, normals, feat_thresh, corner_thresh);
                    //get feature point if necessary
                    if(cell.feature==0){
                        cell.feature_point.x=cell.feature_point.y=cell.feature_point.z=-1.0;
                    }
                    else{
                        Eigen::Vector3f shift = getTransform(cell);
                        Eigen::MatrixXf M = getNormMat(cell, normals);
                        Eigen::VectorXf B = getVec(cell, normals, shift);
                        Eigen::Vector3f feat_pnt = computePoint(M, B, shift, (cell.feature==1));
                        cell.feature_point.x=feat_pnt[0];
                        cell.feature_point.y=feat_pnt[1];
                        cell.feature_point.z=feat_pnt[2];
                    }
                }
                //add cell to list
                cells.push_back(cell);
            }
        }
    }
    return cells;
}

//populate triangles in grid
void Polygonise(GRIDCELL &grid, float isolevel, const bool USING_FEATURES){
    /*
        Determine the index into the edge table which
        tells us which vertices are inside of the surface
    */
    int cubeindex = 0;
    if (grid.val[0] < isolevel) cubeindex |= 1;
    if (grid.val[1] < isolevel) cubeindex |= 2;
    if (grid.val[2] < isolevel) cubeindex |= 4;
    if (grid.val[3] < isolevel) cubeindex |= 8;
    if (grid.val[4] < isolevel) cubeindex |= 16;
    if (grid.val[5] < isolevel) cubeindex |= 32;
    if (grid.val[6] < isolevel) cubeindex |= 64;
    if (grid.val[7] < isolevel) cubeindex |= 128;

    /* Cube is entirely in/out of the surface */
    if (edgeTable[cubeindex] == 0){
        grid.isSurface=false;
        return;
    }
    grid.isSurface=true;

    XYZ vertlist[12];
    /* Find the vertices where the surface intersects the cube */
    if (edgeTable[cubeindex] & 1)
      vertlist[0] =
         VertexInterp(isolevel,grid.p[0],grid.p[1],grid.val[0],grid.val[1]);
    if (edgeTable[cubeindex] & 2)
      vertlist[1] =
         VertexInterp(isolevel,grid.p[1],grid.p[2],grid.val[1],grid.val[2]);
    if (edgeTable[cubeindex] & 4)
      vertlist[2] =
         VertexInterp(isolevel,grid.p[2],grid.p[3],grid.val[2],grid.val[3]);
    if (edgeTable[cubeindex] & 8)
      vertlist[3] =
         VertexInterp(isolevel,grid.p[3],grid.p[0],grid.val[3],grid.val[0]);
    if (edgeTable[cubeindex] & 16)
      vertlist[4] =
         VertexInterp(isolevel,grid.p[4],grid.p[5],grid.val[4],grid.val[5]);
    if (edgeTable[cubeindex] & 32)
      vertlist[5] =
         VertexInterp(isolevel,grid.p[5],grid.p[6],grid.val[5],grid.val[6]);
    if (edgeTable[cubeindex] & 64)
      vertlist[6] =
         VertexInterp(isolevel,grid.p[6],grid.p[7],grid.val[6],grid.val[7]);
    if (edgeTable[cubeindex] & 128)
      vertlist[7] =
         VertexInterp(isolevel,grid.p[7],grid.p[4],grid.val[7],grid.val[4]);
    if (edgeTable[cubeindex] & 256)
      vertlist[8] =
         VertexInterp(isolevel,grid.p[0],grid.p[4],grid.val[0],grid.val[4]);
    if (edgeTable[cubeindex] & 512)
      vertlist[9] =
         VertexInterp(isolevel,grid.p[1],grid.p[5],grid.val[1],grid.val[5]);
    if (edgeTable[cubeindex] & 1024)
      vertlist[10] =
         VertexInterp(isolevel,grid.p[2],grid.p[6],grid.val[2],grid.val[6]);
    if (edgeTable[cubeindex] & 2048)
      vertlist[11] =
         VertexInterp(isolevel,grid.p[3],grid.p[7],grid.val[3],grid.val[7]);


    if(!USING_FEATURES){
        /* Create the triangles */
        for (int i=0; triTable[cubeindex][i]!=-1; i+=3) {
            TRIANGLE t;
            t.p[0] = vertlist[triTable[cubeindex][i  ]];
            t.p[1] = vertlist[triTable[cubeindex][i+1]];
            t.p[2] = vertlist[triTable[cubeindex][i+2]];
            //check if triangle is degenerate
            if(XYZ::dist(t.p[0],t.p[1])<0.00001) continue;
            if(XYZ::dist(t.p[1],t.p[2])<0.00001) continue;
            if(XYZ::dist(t.p[2],t.p[0])<0.00001) continue;
            grid.triangles.push_back(t);
        }
    }

    else{
        //if grid has no feature,
        if(grid.feature=0){
            /* Create the triangles */
            for (int i=0; triTable[cubeindex][i]!=-1; i+=3) {
                TRIANGLE t;
                t.p[0] = vertlist[triTable[cubeindex][i  ]];
                t.p[1] = vertlist[triTable[cubeindex][i+1]];
                t.p[2] = vertlist[triTable[cubeindex][i+2]];
                //check if triangle is degenerate
                if(XYZ::dist(t.p[0],t.p[1])<0.00001) continue;
                if(XYZ::dist(t.p[1],t.p[2])<0.00001) continue;
                if(XYZ::dist(t.p[2],t.p[0])<0.00001) continue;
                grid.triangles.push_back(t);
            }
        }

        //grid has feature
        //create triangle fan from feature point
        else{
            /* Create triangles */
            vector<TRIANGLE> temp_tri;
            for (int i=0; triTable[cubeindex][i]!=-1; i+=3) {
                TRIANGLE t;
                t.p[0] = vertlist[triTable[cubeindex][i  ]];
                t.p[1] = vertlist[triTable[cubeindex][i+1]];
                t.p[2] = vertlist[triTable[cubeindex][i+2]];
                //check if triangle is degenerate
                if(XYZ::dist(t.p[0],t.p[1])<0.00001) continue;
                if(XYZ::dist(t.p[1],t.p[2])<0.00001) continue;
                if(XYZ::dist(t.p[2],t.p[0])<0.00001) continue;
                temp_tri.push_back(t);
            }
            //get list of edges
            vector<vector<XYZ> > edges = getEdges(temp_tri);
            vector<vector<XYZ> > new_edges;
            //edges with duplicates are removed
            for(int i=0; i<edges.size(); i++){
                bool add_edge=true;
                for(int j=0; j<edges.size(); j++){
                    if(i!=j){
                        //check if edges are the same
                        if(XYZ::dist(edges[i][0],edges[j][0])<0.00001){
                            if(XYZ::dist(edges[i][1],edges[j][1])<0.00001) add_edge=false;
                        }
                    }
                }
                if(add_edge) new_edges.push_back(edges[i]);
            }
            //create new triangles from edges and feature point
            for(int i=0; i<new_edges.size(); i++){
                TRIANGLE t;
                t.p[0]=new_edges[i][0];
                t.p[1]=new_edges[i][1];
                t.p[2]=grid.feature_point;
                //check if triangle is degenerate
                if(XYZ::dist(t.p[0],t.p[1])<0.00001) continue;
                if(XYZ::dist(t.p[1],t.p[2])<0.00001) continue;
                if(XYZ::dist(t.p[2],t.p[0])<0.00001) continue;
                grid.triangles.push_back(t);
            }
        }
    }

}

XYZ VertexInterp(float isolevel, XYZ p1, XYZ p2, float valp1, float valp2){
    if (p2<p1){
        XYZ temp;
        temp = p1;
        p1 = p2;
        p2 = temp;
        float tempval = valp1;
        valp1=valp2;
        valp2=tempval;
    }

    XYZ p;
    if(fabs(valp1 - valp2) > 0.00001){
        p.x = p1.x + (p2.x - p1.x)/(valp2 - valp1)*(isolevel - valp1);
        p.y = p1.y + (p2.y - p1.y)/(valp2 - valp1)*(isolevel - valp1);
        p.z = p1.z + (p2.z - p1.z)/(valp2 - valp1)*(isolevel - valp1);
    }
    else{
        p.x = p1.x;
        p.y = p1.y;
        p.z = p1.z;
    }
    return p;
}

//get list of edges from triangles
//edges are sorted using XYZ<
vector<vector<XYZ> > getEdges(const vector<TRIANGLE> &triangles){
    vector<vector<XYZ> > edges;
    for(int i=0; i<triangles.size(); i++){
        for(int j=0; j<3; j++){
            vector<XYZ> edge;
            edge.push_back(triangles[i].p[j]);
            edge.push_back(triangles[i].p[(j+1)%3]);
            //make sure edges are sorted
            if(edge[1]<edge[0]){
                XYZ temp;
                temp = edge[0];
                edge[0] = edge[1];
                edge[1] = temp;
            }
            edges.push_back(edge);
        }
    }
    return edges;
}

//check if edges are the same
bool sameEdge(const vector<XYZ> &lhs, const vector<XYZ> &rhs){
    return ((XYZ::dist(lhs[0],rhs[0])<0.00001) && (XYZ::dist(lhs[1],rhs[1])<0.00001));
}

vector<TRIANGLE> flipEdges(vector<GRIDCELL> &cells, gridPtr in){
    vector<TRIANGLE> new_triangles;
    int dim_x = in->dims[0]-1;
    int dim_y = in->dims[1]-1;
    int dim_z = in->dims[2]-1;
    //perform edge flipping on feature cells
    for(int cell_ind=0; cell_ind<cells.size(); cell_ind++){
        int x=cell_ind/(dim_y*dim_z);
        int y=(cell_ind%(dim_y*dim_z))/dim_z;
        int z=(cell_ind%(dim_y*dim_z))%dim_z;

        //make sure cell is a feature cell
        if(cells[cell_ind].feature==0) continue;

        /* cell is a feature cell */

        //get neighbor cell indexes
        vector<int> neighbor_inds;
        if(x!=0){
            int index = (x-1)*(dim_y*dim_z)+(y)*(dim_z)+(z);
            neighbor_inds.push_back(index);
        }
        if(x!=dim_x-1){
            int index = (x+1)*(dim_y*dim_z)+(y)*(dim_z)+(z);
            neighbor_inds.push_back(index);
        }
        if(y!=0){
            int index = (x)*(dim_y*dim_z)+(y-1)*(dim_z)+(z);
            neighbor_inds.push_back(index);
        }
        if(y!=dim_y-1){
            int index = (x)*(dim_y*dim_z)+(y+1)*(dim_z)+(z);
            neighbor_inds.push_back(index);
        }
        if(z!=0){
            int index = (x)*(dim_y*dim_z)+(y)*(dim_z)+(z-1);
            neighbor_inds.push_back(index);
        }
        if(z!=dim_z-1){
            int index = (x)*(dim_y*dim_z)+(y)*(dim_z)+(z+1);
            neighbor_inds.push_back(index);
        }

        //get edges from target grid triangles
        vector<vector<XYZ> > cell_edges = getEdges(cells[cell_ind].triangles);

        //iterate through neighbors
        for(int neighbor_iter=0; neighbor_iter<neighbor_inds.size(); neighbor_iter++){

            //index=index for target cell
            //n_index=index for current neighboring cell
            int neighbor_ind=neighbor_inds[neighbor_iter];

            //make sure neighbor is on surface and a feature cell
            if((!cells[neighbor_ind].isSurface)||(cells[neighbor_ind].feature==0)) continue;

            /* neighbor is on surface and a feature cell */

            //get edges for neighbor cell triangles
            vector<vector<XYZ> > neighbor_edges = getEdges(cells[neighbor_ind].triangles);
            //get all shared edges
            vector<vector<XYZ> > shared_edges;
            for(int i=0; i<cell_edges.size(); i++){
                bool is_shared=false;
                for(int j=0; j<neighbor_edges.size(); j++){
                    //check if edges are the same
                    if(sameEdge(cell_edges[i],neighbor_edges[j])){
                        is_shared=true;
                        break;
                    }
                }
                if(is_shared){
                    shared_edges.push_back(cell_edges[i]);
                }
            }

            /* construct new triangles from shared edges and remove altered triangles from gridcells */
            for(int edge_ind=0; edge_ind<shared_edges.size(); edge_ind++){

                /* create new triangles */
                TRIANGLE t1;
                t1.p[0]=cells[cell_ind].feature_point; t1.p[1]=cells[neighbor_ind].feature_point; t1.p[2]=shared_edges[edge_ind][0];
                TRIANGLE t2;
                t2.p[0]=cells[cell_ind].feature_point; t2.p[1]=cells[neighbor_ind].feature_point; t2.p[2]=shared_edges[edge_ind][1];
                new_triangles.push_back(t1);
                new_triangles.push_back(t2);

                //get indexes of triangles using shared edges
                int cell_tri_index; //index of triangle in target cell
                int neighbor_tri_index; //index of triangle in neighbor cell

                /* search target cell for corresponding triangle */
                bool done_check=false;
                //iterate through triangles
                for(int i=0; i<cells[cell_ind].triangles.size(); i++){
                    vector<TRIANGLE> tr;
                    tr.push_back(cells[cell_ind].triangles[i]);
                    //get edges of triangle
                    vector<vector<XYZ> > tr_edges = getEdges(tr);
                    for(int j=0; j<tr_edges.size(); j++){
                        //check if edges are the same
                        if(sameEdge(shared_edges[edge_ind], tr_edges[j])){
                            cell_tri_index=i;
                            done_check=true;
                            break;
                        }
                    }
                    if(done_check) break;
                }

                /* search neighbor cell for corresponding triangle */
                done_check=false;
                //iterate through triangles
                for(int i=0; i<cells[neighbor_ind].triangles.size(); i++){
                    vector<TRIANGLE> tr;
                    tr.push_back(cells[neighbor_ind].triangles[i]);
                    //get edges of triangle
                    vector<vector<XYZ> > tr_edges = getEdges(tr);
                    for(int j=0; j<tr_edges.size(); j++){
                        //check if edges are the same
                        if(sameEdge(shared_edges[edge_ind], tr_edges[j])){
                            neighbor_tri_index=i;
                            done_check=true;
                            break;
                        }
                    }
                    if(done_check) break;
                }

                /* remove triangles from cells */
                //remove triangle from target cell by shifting all subsequent triangles down
                for(int i=cell_tri_index; i<cells[cell_ind].triangles.size()-1; i++){
                    for(int j=0; j<3; j++){
                        cells[cell_ind].triangles[i].p[j]=cells[cell_ind].triangles[i+1].p[j];
                    }
                    cells[cell_ind].triangles.pop_back();
                }
                //remove triangle from neighbor cell by shifting all subsequent triangles down
                for(int i=neighbor_tri_index; i<cells[neighbor_ind].triangles.size()-1; i++){
                    for(int j=0; j<3; j++){
                        cells[neighbor_ind].triangles[i].p[j]=cells[neighbor_ind].triangles[i+1].p[j];
                    }
                    cells[neighbor_ind].triangles.pop_back();
                }

            }//end of shared edge handling (for loop through shared edges)

        }//done iterating through neighbors (for loop through neighbor cells)

    }//end of edge flipping (for loop through all cells)

    //return new triangles from edge flipping
    return new_triangles;
}


//run marching cubes and save to ply file
void mcubes(gridPtr in, gridPtr surfaceMap, const vector<Eigen::Vector3f> &normals, float isolevel, float feat_thresh, float corner_thresh, const char* filename, const bool USING_FEATURES){
    //get GridCell data
    vector<GRIDCELL> cells = getGridCells(in, surfaceMap, normals, feat_thresh, corner_thresh, USING_FEATURES);
    //populate triangle data
    for(int i=0; i<cells.size(); i++){
        Polygonise(cells[i],isolevel, USING_FEATURES);
    }

    vector<TRIANGLE> new_triangles;
    if(USING_FEATURES){
        new_triangles=flipEdges(cells, in);
    }
    //new_triangles populated with triangles from flipped edges (if using features)

    //populate list of all triangles
    vector<TRIANGLE> all_triangles;
    for(int i=0; i<cells.size(); i++){
        for(int j=0; j<cells[i].triangles.size(); j++){
            all_triangles.push_back(cells[i].triangles[j]);
        }
    }
    for(int i=0; i<new_triangles.size(); i++){
        all_triangles.push_back(new_triangles[i]);
    }
    //list of triangles complete


    //create hashtable for checking point collisions
    pointHashmap indexer(all_triangles, in);
    //all points are now indexed

    //create ply file
    ofstream myfile;
    myfile.open(filename);
    //create header
    myfile<<"ply"<<endl;
    myfile<<"format ascii 1.0"<<endl;
    myfile<<"element vertex "<<indexer.numPoints<<endl;
    myfile<<"property float x"<<endl;
    myfile<<"property float y"<<endl;
    myfile<<"property float z"<<endl;
    myfile<<"element face "<<all_triangles.size()<<endl;
    myfile<<"property list uchar int vertex_indices"<<endl;
    myfile<<"end_header"<<endl;
    //stream points data
    for(int i=0; i<indexer.numPoints; i++){
        myfile<<indexer.points[i].x<<" "<<indexer.points[i].y<<" "<<indexer.points[i].z<<endl;
    }
    //stream triangle data
    for(int i=0; i<all_triangles.size(); i++){
        int index_1 = indexer.index(all_triangles[i].p[0]);
        int index_2 = indexer.index(all_triangles[i].p[1]);
        int index_3 = indexer.index(all_triangles[i].p[2]);
        if(index_1==-1 || index_2==-1 || index_3==-1){
            cerr<<"Hashmap failed. Point not found."<<endl;
            myfile.close();
            exit(1);
        }
        myfile<<"3 "<<index_1<<" "<<index_2<<" "<<index_3<<endl;
    }
    //close file
    myfile.close();

}



