""" Reads a mesh generated by gmsh (.msh file)
    parses it, and outputs a .mesh file
    written by Ben Cumming """

import os
import sys
from UserDict import UserDict
from Node import Node
from Element import Element
from meshDefs import *
import Numeric

def listMakeUnique(seq):
    checked = []
    for e in seq:
        if e not in checked:
            checked.append(e)
    return checked

def faceCompare(f1,f2):
    for i in range(len(f1)):
        if f1[i]!=f2[i]:
            return False
    return True

def findBinaryInsert(index, list):
    n = len(list)
    minPos=0
    maxPos=n-1
    # check for the trivial cases first
    if n==0 or index<=list[0]:
        return 0
    if index>=list[maxPos]:
        return n

    # perform the search
    while True:
        if (maxPos-minPos)<2:
            return maxPos
        centre = (maxPos+minPos)/2
        val = list[centre]
        if val<index:
            minPos=centre
        elif val>index:
            maxPos=centre
        else:
            return centre

def findBinary(index, list):
    n = len(list)
    minPos=0
    maxPos=n-1
    # check for the trivial cases first
    if index<list[0] or index>list[maxPos]: # index is out of bounds
        return -1
    if index==list[0]: # index is the first entry in list
        return 0
    if index==list[maxPos]: # index is the last entry in list
        pos = maxPos
        while (pos>0) and (list[pos-1]==index):
            pos -= 1
        return pos

    # perform the search
    while True:
        if (maxPos-minPos)<2:
            return -1
        centre = (maxPos+minPos)/2
        val = list[centre]
        if val<index:
            minPos=centre
        elif val>index:
            maxPos=centre
        else:
            pos=centre
            while (pos>0) and (list[pos-1]==index):
                pos -= 1
            return pos

class FileInfo(UserDict):
    def __init__(self, filename=None):
        UserDict.__init__(self)
        self["name"] = filename

class Mesh(FileInfo):
    "handles a gmsh .msh file"
    def __init__(self, fileName=None):
        FileInfo.__init__(self)
        self["name"] = fileName
        self["rootName"] = fileName

    def __parse(self, fileName):
        "parse mesh from file"
        self.clear()
        try:
            fsock = open(fileName,"r", 0)
            try:
                meshData = fsock.read().split("\n")
            finally:
                fsock.close()
            # load elements first, this is neccesary to determine the boundary nodes
            print "\treading elements"
            self.readElements(meshData[meshData.index("$Elements")+1:meshData.index("$EndElements")])
            # now load the nodes
            print "\treading nodes"
            self.readNodes(meshData[meshData.index("$Nodes")+1:meshData.index("$EndNodes")])
            # relable the nodes in the elements to reflect the new contiguous ordering
            print "\trelabling element nodes"
            self.relableElementNodes()
            # sort the elements such that those on the boundary are first
            print "\tsorting the elements"
            self.sortElements()
            # take the faces, and associate them with the relevant elements
            print "\tassociating boundary faces with elements"
            self.associateFaces()
        except IOError:
            print "ERROR loading file : " + fileName
            exit("exiting...")

    def __setitem__(self,key,item):
        if key == "name" and item:
            self.__parse(item+".msh")
        else :
            FileInfo.__setitem__(self,key,item)

    def sortElements(self):
        nFaces = len(self["faces"])
        if nFaces==0: # we don't have to do anything for the case where there are no boundary faces
            print "WARNING : trying to process a mesh with no boundary faces : all face BC tags set to 0"
            return
        nBoundaryNodes = len(self["boundaryNodes"])
        elements=self["elements"]
        boundaryElements = []
        internalElements = []
        for e in elements:
            # check that the element has at least two nodes on the boundary
            m = Numeric.sort(Numeric.array(e.getNodes()))[1]
            if m<nBoundaryNodes:
                boundaryElements.append(e)
            else:
                internalElements.append(e)
        # store the elements such that the boundary elements come first
        self["elements"] = boundaryElements + internalElements

        # now sort the faces in order of smallest node index.
        # the order that the faces are stored is not changed.
        # the new order is reflected in the permutation array faceIndex,
        # and the corresponding minimum node indices are stored in faceMins.
        # These two lists are saved, to be used to speed up the matching of
        # boundary faces to their corresponding element faces later on.
        faceMins = []
        faceIndex = []
        faces = self["faces"]
        nFaces = len(faces)
        nSorted=0
        for i in range(nFaces):
            newNodes = Numeric.sort(Numeric.array(faces[i].getNodes()))
            faces[i].setNodes(newNodes)
            minNode = newNodes[0]
            pos = findBinaryInsert(minNode,faceMins)
            faceMins.insert(pos,minNode)
            faceIndex.insert(pos,i)
        # construct the faceIndex from the temporary index
        self["faceMins"] = faceMins
        self["faceIndex"] = faceIndex

    def readElements(self,elementData):
        elements=[]
        faces=[]
        iFace=0
        iElement=0
        # find the total number of elements generated by gmsh
        nElements = int(elementData[0])
        # determine if this is a 2D or 3D mesh by searching through the elements
        meshDim = 2
        elementTypes = elementTypes2D
        faceTypes = faceTypes2D
        for txt in elementData[1:]:
            # get the info for this element, stripping off the leading element number
            element = [int(x) for x in txt.split()[1:]]
            # what type of element have we got?
            eType = element[0]
            if eType in elementTypes3D:
                meshDim = 3
                elementTypes = elementTypes3D
                faceTypes = faceTypes3D
                break
        self["meshDim"] = meshDim
        for txt in elementData[1:]:
            # get the info for this element, stripping off the leading element number
            element = [int(x) for x in txt.split()[1:]]
            # what type of element have we got?
            eType = element[0]
            nTags = element[1]
            tag = element[2]
            if (eType in faceTypes) and (tag!=0): # we have a face element that lies on a boundary
                nodes = element[2+nTags:]
                faces.append(Element(eType,nodes,tag,[]))
                iFace += 1
            elif eType in elementTypes: # we have an element
                nodes = element[2+nTags:]
                elements.append(Element(eType,nodes,tag,[]))
                iElement += 1
        print "\t  ", iElement, "Elements and ", iFace, " Faces"
        self["elements"]=elements
        self["faces"]=faces
        # now make a list of nodes that lie on boundaries
        bNodes = {}
        bNodeTags = {}
        nIndex=0
        for f in faces:
            fNodes = f.getNodes()
            for n in fNodes:
                if n not in bNodes:
                    bNodes[n] = nIndex
                    bNodeTags[nIndex+1] = [f.getElementProps()];
                    nIndex += 1
                else:
                    bNodeTags[bNodes[n]+1].append(f.getElementProps())
        self["boundaryNodes"]=bNodes
        self["boundaryNodeTags"]=bNodeTags

    def readNodes(self,nodeData):
        nodes={}
        index = 0
        nNodes = int(nodeData[0])
        boundaryNodes = self["boundaryNodes"]
        nBoundaryNodes = len(boundaryNodes)
        for txt in nodeData[1:]:
            txt = txt.split()
            key = int(txt[0])
            node =  Node([float(x) for x in txt[1:]],[])
            if key in boundaryNodes:
                nodes[key] = [boundaryNodes[key],node]
            else:
                nodes[key] = [index + nBoundaryNodes,node]
                index += 1
        print "\t  ", index + nBoundaryNodes, " Nodes"
        self["nodes"]=nodes

    def relableElementNodes(self):
        nodes = self["nodes"]
        newElements = []
        for e in self["elements"]:
            e.setNodes( [nodes[j][0] for j in e.getNodes()] )
            newElements.append(e)
        self["elements"] = newElements
        newFaces = []
        for e in self["faces"]:
            e.setNodes( [nodes[j][0] for j in e.getNodes()] )
            newFaces.append(e)
        self["faces"] = newFaces

    def associateFaces(self):
        elements = self["elements"]
        faces = self["faces"]
        if len(faces)==0:
            return
        faceIndex = self["faceIndex"]
        faceMins = self["faceMins"]
        meshDim = self["meshDim"]
        if meshDim==2:
            faceNodeOrders = faceNodeOrders2D
        else:
            faceNodeOrders = faceNodeOrders3D

        numFacesRemaining = len(faces)
        ppos = 0;
        for e in elements:
            eNodes = e.getNodes()
            eType = e.getType()
            tags = e.getFaceProps()
            faceOrder = faceNodeOrders[eType]
            foundFaces = []
            # now loop through the list of boundary faces, testing each one to
            # see if it corresponds to a face of e
            i=0
            for o in faceOrder:
                v = Numeric.sort(Numeric.array(permVec(eNodes,o))) # FASTER
                minNode = v[0]
                # search for starting point
                pos = findBinary(minNode, faceMins)
                if pos>=0: # we have a match
                    # search each potential face in turn
                    haveMatch=False
                    while (not haveMatch) and (pos<numFacesRemaining) and (faceMins[pos]==minNode):
                        fNodes=faces[faceIndex[pos]].getNodes()
                        if len(fNodes)==len(o):
                            haveMatch=faceCompare(v,fNodes)
                        if haveMatch:
                            tags[i] = faces[faceIndex[pos]].getElementProps()
                            # print faces[pos].getElementProps()
                            foundFaces.append(pos)
                        pos += 1
                i+=1

            # save the face tags back to the element list
            elements[ppos].setFaceProps(tags);

            # update the list of boundary faces yet to be associated
            numFacesRemaining -= len(foundFaces)
            if numFacesRemaining==0:
                break

            # remove found faces from our index
            # sorting and reversing is important!
            foundFaces.sort()
            foundFaces.reverse()
            for j in foundFaces:
                del faceMins[j]
                del faceIndex[j]
            ppos += 1;

        self['elements'] = elements;

    def outputMesh(self):
        try:
            fileName = self["rootName"] + r".nodes"
            fsock = open(fileName,"w", 0)

            # write the nodes
            # appends a redundant nodeTag to each node... this can be used at a later date
            nBoundaryNodes=len(self["boundaryNodes"])
            boundaryNodeTags =self["boundaryNodeTags"]
            nodes=range(len(self["nodes"]))
            tags=range(len(self["nodes"]))

            counter=0
            for node in self["nodes"].values():
                nodes[node[0]] = node[1].getCoords()
            for node in nodes:
                if counter<nBoundaryNodes:
                    # tags = listMakeUnique(boundaryNodeTags[counter+1])
                    # text = ["%-20.10g " % c for c in node] + [" %d " % len(tags)] + [" %d" % c for c in tags]
                    text = ["%-20.10g " % c for c in node] + ["-1"]
                else:
                    text = ["%-20.10g " % c for c in node] + ["0"]
                line = ""
                for c in text:
                    line += c
                line += "\n"
                fsock.write(line)
                counter+=1

            fileName = self["rootName"] + r".elements"
            fsock = open(fileName,"w", 0)

            # write the elements
            for element in self["elements"]:
                text = ["%2d " % element.getType()] + ["%3d " % element.getElementProps()] + ["%7d " % c for c in element.getNodes()] + [" %3d" % c for c in element.getFaceProps()]
                line = ""
                for c in text:
                    line += c
                line += "\n"
                fsock.write(line)

            #######################################
            # output mesh as used by the decomp code
            #######################################

            # open file
            fileName = self["rootName"] + r".mesh"
            fsock = open(fileName,"w", 0)

            # write header
            nNodes = len(self["nodes"])
            nElements = len(self["elements"])
            line = "%d " % nNodes + "%d" % nElements
            line += "\n"
            fsock.write(line)


            # write node info
            counter=0
            for node in nodes:
                if counter<nBoundaryNodes:
                    tags = listMakeUnique(boundaryNodeTags[counter+1])
                    text = ["%-20.10g " % c for c in node] + [" %d " % len(tags)] + [" %d" % c for c in tags]
                else:
                    text = ["%-20.10g " % c for c in node] + ["0"]
                line = ""
                for c in text:
                    line += c
                line += "\n"
                fsock.write(line)
                counter+=1

            # write the elements
            for element in self["elements"]:
                text = ["%2d " % element.getType()] + ["%3d " % element.getElementProps()] + ["%7d " % c for c in element.getNodes()] + [" %3d" % c for c in element.getFaceProps()]
                line = ""
                for c in text:
                    line += c
                line += "\n"
                fsock.write(line)

        except IOError:
            print "ERROR opening file for output of the mesh : ", fileName

if __name__ == "__main__":
    fileName = sys.argv[1]
    m = Mesh(fileName)
    m.outputMesh()
